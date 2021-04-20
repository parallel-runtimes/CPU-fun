
CXX=g++-10

omp_scan:

runScan: runscan.py large.txt
	runscan.py

large.txt:
	python3 generateText.py 100 100000 > large.txt

%: %.cc Makefile
	$(CXX) -fopenmp -O3 -o $@ $<
