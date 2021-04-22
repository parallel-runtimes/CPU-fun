/*
 * A simple program to look at implementations of code written in OpenMP
 * which scans lines of text from a file.
 * The aim is to play with different ways of streaming data from a file.
 * The actual operation performed on each line is of less interest.
 * 
 * -- Jim Cownie
 * License: Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* To make this somewhat useful, use the C++ standard regex library
 * to match (and count) matches.
 */

#include <string>
#include <iostream>
#include <regex>
#include <omp.h>

// Using std::string is unlikely to be the fastest way to handle this,
// since it leads to lots of memory allocation/deallocation.

// Better would be to allocate big chunks of memory (or, simply mmap the whole file),
// and slice it up into lines referenced by pointers to start/end.
// But, this is simpler...
static bool getLine(std::string &line) {
  std::getline(std::cin, line);
  return !std::cin.eof();
}

// See https://en.cppreference.com/w/cpp/regex for details of how to
// use the std::regex class.
static bool lineMatches(std::regex const re, std::string & line) {
  return std::regex_search(line, re);
}

// A class to handle our results.
class fileStats {
  int lines;
  int matchedLines;
 public:
  fileStats() : lines(0), matchedLines(0) {}

  void zero() { lines=0; matchedLines=0; }
  int getLines() const { return lines; }
  int getMatchedLines() const { return matchedLines; }
  
  void incLines() { lines++; }
  void incMatchedLines() { matchedLines++; }
  void atomicIncMatchedLines() {
    #pragma omp atomic
    matchedLines++;
  }
  fileStats & operator+=(fileStats const & other) {
    lines += other.lines;
    matchedLines += other.matchedLines;

    return *this;
  }

  void criticalAdd(fileStats & other) {
#pragma omp critical (addStats)
    *this += other;
  }
};

// The obvious, simple, serial code
static fileStats runSerial(std::regex const &matchRE) {
  std::string line;
  fileStats res;
    
  while (getLine(line)) {
    res.incLines();
    
    if (lineMatches(matchRE, line)){
      res.incMatchedLines();
    }
  }
  return res;
}

// Wrap the getLine call in a critical section.
static bool criticalGetLine(std::string & line) {
  bool res;
#pragma omp critical (getLineLock)
  {
    res = getLine(line);
  }
  return res;
}

// A simple parallel version; very similar to the serial one
// except that we have to explicitly serialise reading and
// combining per-thread results.
// See below for a version using a user-defined reduction, which
// is even closer to the serial version.
static fileStats runParallel(std::regex const &matchRE) {
  fileStats fullRes;
  
#pragma omp parallel shared(fullRes, matchRE)
  {
    std::string line;
    fileStats res;

    while (criticalGetLine(line)) {
      res.incLines();
    
      if (lineMatches(matchRE, line)){
        res.incMatchedLines();
      }
    }
    // Accumulate
#pragma omp critical (accumulateRes)
    fullRes += res;
  }
  return fullRes;
}

static fileStats runParallelRed(std::regex const &matchRE) {
  fileStats res;
  
#pragma omp declare reduction (+: fileStats : omp_out += omp_in)
#pragma omp parallel shared(matchRE), reduction(+:res)
  {
    std::string line;

    while (criticalGetLine(line)) {
      res.incLines();

      if (lineMatches(matchRE, line)){
        res.incMatchedLines();
      }
    }
  }
  return res;
}

//
// Something closer to the "I want two separate teams, with one
// thread reading, and others processing lines from a queue."
//
#include <deque>

template<typename T> class lockedDeque {
  std::deque<T> theDeque;
  bool empty() const {
    bool res;
#pragma omp critical (queue)
    res = theDeque.empty();
    return res;
  }
  
 public:
  void push_front(T value) {
#pragma omp critical (queue)
    theDeque.push_front(value);
  }
  T pull_back() {
    T res;
#pragma omp critical (queue)
    {
      if (theDeque.empty()) {
        res = 0;
      } else {
        res = theDeque.back();
        theDeque.pop_back();
      }
    }
    return res;
  }
};

// One thread reads the lines and adds them to shared queue, moving on
// to processing lines once the file is exhausted.
// Other threads try to pull lines from the queue.
// This will still work with only one thread, but in that case it buffers
// the whole file, which may be sub-optimal!
static fileStats runParallelQueue(std::regex const &matchRE) {
  fileStats res;
  lockedDeque<std::string *> lineQueue;
  bool done = false;

  // Use the same reduction operations as before.
#pragma omp declare reduction (+: fileStats : omp_out += omp_in)
#pragma omp parallel shared(matchRE, lineQueue, done), reduction(+:res)
  {
#pragma omp single nowait
    {
      // Read input and produce lines.
      std::string * line = new std::string;
      while (getLine(*line)) {
        lineQueue.push_front(line);
        line = new std::string;
      }
      done = true;
#pragma omp flush
      delete line;
    } // single
    // Consume lines.
    for (;;) {
      std::string * line = lineQueue.pull_back();
      if (line) {
        res.incLines();
        if (lineMatches(matchRE, *line)) {
          res.incMatchedLines();
        }
        delete line;
      } else if (done) {
          break;
      }
    }
  }
  return res;
}

// Tasks using critical on each line to update global state.
static fileStats runOmpTasksCritical(std::regex const &matchRE) {
  fileStats res;
  
#pragma omp parallel
  {
#pragma omp single nowait
    {
      std::string * line = new std::string();
      while (getLine(*line)) {
        res.incLines();
        
#pragma omp task default(none),firstprivate(line),\
  shared(matchRE, res)
        {
          if (lineMatches(matchRE, *line)) {
                res.atomicIncMatchedLines();
           delete line;
          }
        } // task
        line = new std::string();
      } // while we can read a line
      delete line; // Last one isn't passed to a task */
    }
  } // parallel
  return res;
}

// Do the reduction into per-thread static variables, then accumulate them
// at the end.
// If our tasks were untied, accessing thread privat state would
// be undefined behaviour, however, by default tasks are tied so this is fine.
static fileStats runOmpTasksTR(std::regex const &matchRE) {
  fileStats res;
#if (__GNUC__)
  // Work around GCC bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=27557
  // (Scroll near the end...)
  static thread_local fileStats threadRes;
#else
  static fileStats threadRes;
#pragma omp threadprivate (threadRes)
#endif
    
#pragma omp parallel shared(matchRE,res)
  {
    // Only needed if this function is invoked more than once,
    // but for cleanliness we do it, and it's cheap anyway.
    threadRes.zero();
    
    #pragma omp single
    {
      std::string * line = new std::string();
      while (getLine(*line)) {
        res.incLines();
        
#pragma omp task default(none),firstprivate(line),shared(matchRE)
        {
          if (lineMatches(matchRE, *line)) {
            threadRes.incMatchedLines();
          }
          delete line;
        } // task
        line = new std::string();
      } // while we can read a line
      delete line; // Last one isn't passed to a task */
    }
    // Reduce all the per-thread variables.
#pragma omp critical (threadReduce)
    res += threadRes;
  } // parallel
  return res;
}

#if (USE_TASKREDUCTION)
// Broken. SEGVs with g++ 10... (could well be broken code, though).
static fileStats runOmpTasksRed(std::regex const &matchRE) {
  fileStats res;
  
#pragma omp declare reduction (+: fileStats : omp_out += omp_in)
#pragma omp parallel shared(matchRE,res)
  {
    #pragma omp single
    {
      std::string * line = new std::string();
#pragma omp taskgroup task_reduction(+:res)      
      while (getLine(*line)) {
        res.incLines();
        
#pragma omp task default(none),firstprivate(line),shared(matchRE), in_reduction(+:res)
        {
          if (lineMatches(matchRE, *line)) {
            res.incMatchedLines();
          }
          delete line;
        } // end of the task
        line = new std::string();
      } // while we can read a line
      delete line; // Last one isn't passed to a task */
    }
  } // parallel
  return res;
}
#endif

// Yes, this could be a std::unordered_map, but that really does seem
// overkill for something with only a few entries which we scan once!
static struct implementation_t {
  std::string name;
  fileStats (*method) (std::regex const &);
} methods [] = {
  {"serial", runSerial},
  {"parallel", runParallel},
  {"parallelRed", runParallelRed},
  {"parallelQ", runParallelQueue},
  {"taskCritical", runOmpTasksCritical},
  {"taskTR", runOmpTasksTR},
#if (USE_TASKREDUCTION)
  {"taskRed", runOmpTasksRed},
#endif  
};

static implementation_t * findImplementation(char const * name) {
  for (auto &m : methods) {
    if (m.name == name) {
      return &m;
    }
  }
  return 0;
}

static void printHelp() {
  std::cerr << "Need two arguments:\n"
    "  implementation: one of ";
  auto numMethods = sizeof(methods)/sizeof(methods[0]);
  for (int i=0; i<numMethods-1; i++) {
    std::cerr << methods[i].name << ", ";
  }
  std::cerr << methods[numMethods-1].name << "\n";
  std::cerr << "  regular expression\n";
}

int main (int argc, char ** argv) {
  if (argc < 3) {
    printHelp();
    return 1;
  }

  auto impl = findImplementation(argv[1]);
  if (!impl) {
    printHelp();
    return 1;
  }

  try {
    std::regex matchRE (argv[2], std::regex::grep);
#if (PRINT_TIME)
    auto start = omp_get_wtime();
    // Do the work!
    auto res = impl->method(matchRE);
    auto elapsed = omp_get_wtime()-start;
#else
    // Do the work!
    auto res = impl->method(matchRE);
#endif

    std::cout << impl->name << " (" << omp_get_max_threads() << ")" <<
      " Total Lines: " << res.getLines() <<
      ", Matching Lines: " << res.getMatchedLines() << std::endl;
#if (PRINT_TIME)
    std::cerr << "Time" << std::endl <<
      impl->name << std::endl <<
      "Threads,     Time" << std::endl <<
      omp_get_max_threads() << ", " << elapsed << " s" << std::endl;
#endif
    
    return 0;
  } catch (const std::regex_error& e) {
    std::cerr << "Invalid regular expression: " << e.what() << '\n';
  }
  return 1;
}
