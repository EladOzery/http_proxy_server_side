#include "threadpool.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


threadpool *create_threadpool(int num_threads_in_pool) {
    // Validate input
    if (num_threads_in_pool < 1 || num_threads_in_pool > MAXT_IN_POOL) {
        fprintf(stderr, "Error: Invalid pool size\n");
        return NULL;  // Invalid input
    }

    // Allocate memory for the thread pool structure
    threadpool *pool = (threadpool *) malloc(sizeof(threadpool));
    if (!pool) {
        perror("Error: malloc failed for thread pool");
        exit(EXIT_FAILURE);
    }

    // Initialize the thread pool attributes
    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->threads = (pthread_t *) malloc(sizeof(pthread_t) * num_threads_in_pool);
    if (pool->threads == NULL) {
        perror("Error: malloc failed for thread IDs");
        free(pool); // Free previously allocated memory for the thread pool structure
        exit(EXIT_FAILURE);
    }
    pool->qhead = pool->qtail = NULL;
    pool->shutdown = 0;
    pool->dont_accept = 0;

    // Initialize mutex and condition variables
    if (pthread_mutex_init(&(pool->qlock), NULL) != 0) {
        perror("Error: pthread_mutex_init");
        free(pool->threads); // Free previously allocated memory for thread IDs
        free(pool); // Free previously allocated memory for the thread pool structure
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&(pool->q_not_empty), NULL) != 0) {
        perror("Error: pthread_cond_init");
        pthread_mutex_destroy(&(pool->qlock)); // Destroy mutex
        free(pool->threads); // Free previously allocated memory for thread IDs
        free(pool); // Free previously allocated memory for the thread pool structure
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&(pool->q_empty), NULL) != 0) {
        perror("Error: pthread_cond_init");
        pthread_mutex_destroy(&(pool->qlock)); // Destroy mutex
        pthread_cond_destroy(&(pool->q_not_empty)); // Destroy condition variable
        free(pool->threads); // Free previously allocated memory for thread IDs
        free(pool); // Free previously allocated memory for the thread pool structure
        exit(EXIT_FAILURE);
    }

    // Create worker threads
    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, do_work, (void *) pool) != 0) {
            perror("Error: pthread_create");
            // Cleanup resources
            for (int j = 0; j < i; j++) {
                pthread_cancel(pool->threads[j]);
            }
            pthread_mutex_destroy(&(pool->qlock)); // Destroy mutex
            pthread_cond_destroy(&(pool->q_not_empty)); // Destroy condition variable
            pthread_cond_destroy(&(pool->q_empty)); // Destroy condition variable
            free(pool->threads); // Free previously allocated memory for thread IDs
            free(pool); // Free previously allocated memory for the thread pool structure
            exit(EXIT_FAILURE);
        }
    }

    return pool;
}


//threadpool *create_threadpool(int num_threads_in_pool) {
//    if (num_threads_in_pool < 1 || num_threads_in_pool > MAXT_IN_POOL) {
//        printf("Error: Invalid pool side\n");
//        return NULL;  // Invalid input
//    }
//
//    threadpool *pool = (threadpool *) malloc(sizeof(threadpool));
//    if (!pool) {
//        perror("pool malloc\n");
//        exit(EXIT_FAILURE);
//    }
//
//    pool->num_threads = num_threads_in_pool;
//    pool->qsize = 0;
//    pool->threads = (pthread_t *) malloc(sizeof(pthread_t) * num_threads_in_pool);
//    if(pool->threads == NULL){
//        perror("pool->threads malloc\n");
//        exit(EXIT_FAILURE);
//    }
//
//    pool->qhead = pool->qtail = NULL;
//    pool->shutdown = 0;
//    pool->dont_accept = 0;
//
//    // Initialize mutex and condition variables
//    if (pthread_mutex_init(&(pool->qlock), NULL) != 0){
//        perror("Error: pthread_mutex_init\n;");
//        exit(EXIT_FAILURE);
//    }
//
//    if (pthread_cond_init(&(pool->q_not_empty), NULL)!= 0){
//        perror("Error: pthread_mutex_init\n;");
//        exit(EXIT_FAILURE);
//    }
//
//    if (pthread_cond_init(&(pool->q_empty), NULL) != 0){
//        perror("Error: pthread_mutex_init\n;");
//        exit(EXIT_FAILURE);
//    }
//
//    // Create worker threads
//    for (int i = 0; i < num_threads_in_pool; i++) {
//        if (pthread_create(&(pool->threads[i]), NULL, do_work, (void *) pool) != 0){
//            perror("Error: pthread_create\n");
//        }
//    }
//
//    return pool;
//}

void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
    work_t *work = (work_t *) malloc(sizeof(work_t));
    if (!work) {
        return;  // Memory allocation failed
    }

    /* create work structure */
    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;

    pthread_mutex_lock(&(from_me->qlock));

    /* if we don't accept anymore works */
    if (!from_me || from_me->dont_accept) {
        pthread_mutex_unlock(&(from_me->qlock));
        free(work);  // No need to free here
        return;  // Pool is not valid or destruction has begun
    }


    /* if queue size == 0, add work to queue and broadcast that queue isn't empty */
    if (from_me->qsize == 0) {
        from_me->qhead = from_me->qtail = work;
        pthread_cond_broadcast(&(from_me->q_not_empty));
    }

        /* add work to queue*/
    else {
        from_me->qtail->next = work;
        from_me->qtail = work;
    }

    // increase work queue size
    from_me->qsize++;

    pthread_mutex_unlock(&(from_me->qlock));
}


//void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
//    work_t *work = (work_t *) malloc(sizeof(work_t));
//    if (!work) {
//        return;  // Memory allocation failed
//    }
//
//    pthread_mutex_lock(&(from_me->qlock));
//
//    /* if we don't accept anymore works */
//    if (!from_me || from_me->dont_accept) {
//        pthread_mutex_unlock(&(from_me->qlock));
//        free(work);
//        return;  // Pool is not valid or destruction has begun
//    }
//
//    /* create work structure */
//    work->routine = dispatch_to_here;
//    work->arg = arg;
//    work->next = NULL;
//
//    /* if queue size == 0, add work to queue and broadcast that queue isn't empty */
//    if (from_me->qsize == 0) {
//        from_me->qhead = from_me->qtail = work;
//        pthread_cond_broadcast(&(from_me->q_not_empty));
//    }
//
//    /* add work to queue*/
//    else {
//        from_me->qtail->next = work;
//        from_me->qtail = work;
//    }
//
//    // increase work queue size
//    from_me->qsize++;
//
//    pthread_mutex_unlock(&(from_me->qlock));
//
//    free(work);
//}


void *do_work(void *p) {
    threadpool *pool = (threadpool *) p;

    while (1) {
        pthread_mutex_lock(&(pool->qlock));

        while (pool->qsize == 0 && !pool->shutdown) {
            pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock));
        }

        if (pool->shutdown && pool->qsize == 0) {
            pthread_mutex_unlock(&(pool->qlock));
            pthread_exit(NULL);
        }

        work_t *work = pool->qhead;
        pool->qhead = work->next;

        if (pool->qsize == 1) {
            pool->qtail = NULL;
        }

        pool->qsize--;

        pthread_mutex_unlock(&(pool->qlock));

        work->routine(work->arg);
        free(work);
    }
}

//void destroy_threadpool(threadpool *destroyme) {
//    if (!destroyme || destroyme->dont_accept) {
//        return;  // Pool is not valid or destruction has begun
//    }
//
//    pthread_mutex_lock(&(destroyme->qlock));
//
//    destroyme->dont_accept = 1;
//
//    /* waiting for all works to be done */
//    while (destroyme->qsize > 0) {
//        printf("Waiting for %d works to be done...\n", destroyme->qsize);
//        pthread_cond_wait(&(destroyme->q_empty), &(destroyme->qlock));
//    }
//
//    destroyme->shutdown = 1;
//    pthread_cond_broadcast(&(destroyme->q_not_empty));
//    pthread_mutex_unlock(&(destroyme->qlock));
//
//    for (int i = 0; i < destroyme->num_threads; i++) {
//        pthread_join(destroyme->threads[i], NULL);
//    }
//
//    free(destroyme->threads);
//    free(destroyme);
//
//    destroyme = NULL;
//
//    printf("Threadpool destroyed.\n");
//}

void destroy_threadpool(threadpool *destroyme) {
    if (!destroyme || destroyme->dont_accept) {
        return;  // Pool is not valid or destruction has begun
    }

    pthread_mutex_lock(&(destroyme->qlock));

    destroyme->dont_accept = 1;

    /* Signal threads to exit */
    destroyme->shutdown = 1;
    pthread_cond_broadcast(&(destroyme->q_not_empty));
    pthread_mutex_unlock(&(destroyme->qlock));

    for (int i = 0; i < destroyme->num_threads; i++) {
        pthread_join(destroyme->threads[i], NULL);
    }

    free(destroyme->threads);
    free(destroyme);

    destroyme = NULL;

    printf("Threadpool destroyed.\n");
}
