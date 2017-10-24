/* C program for Merge Sort */
// from http://www.geeksforgeeks.org/merge-sort/
#include<stdlib.h>
#include<stdio.h>
#include "uthread.h"

// Merges two subarrays of arr[].
// First subarray is arr[l..m]
// Second subarray is arr[m+1..r]
void merge(int arr[], int l, int m, int r)
{
    int i, j, k;
    int n1 = m - l + 1;
    int n2 =  r - m;

    /* create temp arrays */
    int L[n1], R[n2];

    /* Copy data to temp arrays L[] and R[] */
    for (i = 0; i < n1; i++)
        L[i] = arr[l + i];
    for (j = 0; j < n2; j++)
        R[j] = arr[m + 1+ j];

    /* Merge the temp arrays back into arr[l..r]*/
    i = 0; // Initial index of first subarray
    j = 0; // Initial index of second subarray
    k = l; // Initial index of merged subarray
    while (i < n1 && j < n2)
    {
        if (L[i] <= R[j])
        {
            arr[k] = L[i];
            i++;
        }
        else
        {
            arr[k] = R[j];
            j++;
        }
        k++;
    }

    /* Copy the remaining elements of L[], if there
       are any */
    while (i < n1)
    {
        arr[k] = L[i];
        i++;
        k++;
    }

    /* Copy the remaining elements of R[], if there
       are any */
    while (j < n2)
    {
        arr[k] = R[j];
        j++;
        k++;
    }
}

struct merge_params {
    int l;
    int r;
    int* arr;
};

void mergeSort(int arr[], int l, int r);

void* merge_thread(void* arg){
    merge_params params = *(reinterpret_cast<merge_params*>(arg));
    mergeSort(params.arr, params.l, params.r);
    return 0;
}

int uthread_merge(int* arr, int l, int r){
    merge_params* params = (merge_params*)malloc(sizeof(merge_params));

    params->arr = arr;
    params->l = l;
    params->r = r;

    return uthread_create(merge_thread, params);
}

/* l is for left index and r is right index of the
   sub-array of arr to be sortesd */
void mergeSort(int* arr, int l, int r) {
    if (l < r)
    {
        // Same as (l+r)/2, but avoids overflow for
        // large l and h
        int m = l+(r-l)/2;

        // Sort first and second halves
        int tid1 = uthread_merge(arr, l, m);
        int tid2 = uthread_merge(arr,m+1,r);

        void* ptr;
        printf("joining %d\n", tid1);
        uthread_join(tid1, &ptr);
        uthread_join(tid2, &ptr);

        merge(arr, l, m, r);
    }
}



/* UTILITY FUNCTIONS */
/* Function to print an array */
void printArray(int A[], int size) {
    int i;
    for (i=0; i < size; i++)
        printf("%d ", A[i]);
    printf("\n");
}

/* Driver program to test above functions */
int main() {
    int arr[500];
    int arr_size = sizeof(arr)/sizeof(arr[0]);

    printf("Given array is \n");
    printArray(arr, arr_size);

    uthread_merge(arr, 0, arr_size -1);
    uthread_init(1);
    start();

    printf("\nSorted array is \n");
    printArray(arr, arr_size);
    return 0;
}