uthread: main.cpp uthread.cpp uthread.h
	g++ --std=c++11 -o uthread main.cpp uthread.h uthread.cpp -lrt

mergesort: uthread.cpp merge_sort.cpp uthread.h
	g++ -g --std=c++11 -o mergesort merge_sort.cpp uthread.h uthread.cpp -lrt
