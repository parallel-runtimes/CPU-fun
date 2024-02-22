#include <algorithm>
#include <set>
#include <cstdint>
#include <cstdio>
#include <omp.h>


static float parTot(int n, float const * a) {
    float total = 0.0;
    
    #pragma omp parallel for reduction(+:total)
    for (int i=0; i<n; i++)
      total += a[i];

    return total;
}

static float parTotDA(int n, float const * a) {
    double total = 0.0;
    
    #pragma omp parallel for reduction(+:total)
    for (int i=0; i<n; i++)
      total += a[i];

    return total;
}

static float serTot(int n, float const *a ){
    float total = 0.0;
    
    for (int i=0; i<n; i++)
      total += a[i];

    return total;
}

static float serTotDA(int n, float const * a) {
    double total = 0.0;
    
    #pragma omp parallel for reduction(+:total)
    for (int i=0; i<n; i++)
      total += a[i];

    return total;
}

template<class MT> class less {
 public:
  bool operator()(MT const & a, MT const & b) const {
    return std::abs(a) < std::abs(b);
  }
};

static float orderedReduction(int n, float const *a) {
  // Build the sorted container
  std::multiset<float,less<float>> sortedValues;
  for (int i=0; i<n; i++) {
    sortedValues.insert(a[i]);
  }

  // Perform the sorted reduction.
  while (sortedValues.size() != 1) {
    float a = sortedValues.extract (sortedValues.begin()).value();
    float b = sortedValues.extract (sortedValues.begin()).value();

    // printf("%g + %g = %g\n",a,b,a+b);

    sortedValues.insert(a+b);
  }

  return *sortedValues.begin();
}

static void initArray(int n, float *a) {
  a[0] = 1.0;
  a[n-1] = -1.0;
  for (int i=1; i<n-1; i++) {
    a[i] = 2.e-8;
  }
}

int main(int, char **) {
  enum {
    arraySize = 100002
  };
  float * data = new float[arraySize];

  initArray(arraySize, data);

  printf("omp_get_max_threads() %d\n",omp_get_max_threads());
  printf("Serial total: %g, parallel total %g, mathematical result %g\n",
         serTot(arraySize, data), parTot(arraySize, data), (arraySize-2)*2.e-8);
  printf("With double accumulator.\n");
  printf("Serial total: %g, parallel total %g, mathematical result %g\n",
         serTotDA(arraySize, data), parTotDA(arraySize, data), (arraySize-2)*2.e-8);

  printf("orderedReduction value: %g\n", orderedReduction(arraySize, data));
  return 0;
}
