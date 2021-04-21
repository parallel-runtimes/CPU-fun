
CXX=g++-10
CXX=clang++

omp_scan:

run: omp_scan runscan.py large.txt 
	python3	runscan.py

large.txt: Makefile
	python3 generateText.py 50 500000 > large.txt

%: %.cc Makefile
	$(CXX) -fopenmp -O3 -o $@ $<
