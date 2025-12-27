package com.lvonasek.arcore3dscanner.utils;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * Centralized executor service for background tasks
 * Replaces raw Thread creation with reusable thread pools
 */
public class AppExecutors {
    private static volatile AppExecutors instance;

    private ExecutorService diskIO;
    private ExecutorService networkIO;
    private ExecutorService computation;

    private AppExecutors() {
        initExecutors();
    }

    private void initExecutors() {
        diskIO = Executors.newFixedThreadPool(4);
        networkIO = Executors.newFixedThreadPool(2);
        computation = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors());
    }

    public static AppExecutors getInstance() {
        if (instance == null) {
            synchronized (AppExecutors.class) {
                if (instance == null) {
                    instance = new AppExecutors();
                }
            }
        }
        // Reinitialize if executors were shut down
        if (instance.isShutdown()) {
            synchronized (AppExecutors.class) {
                if (instance.isShutdown()) {
                    instance.initExecutors();
                }
            }
        }
        return instance;
    }

    private boolean isShutdown() {
        return (diskIO == null || diskIO.isShutdown()) &&
               (networkIO == null || networkIO.isShutdown()) &&
               (computation == null || computation.isShutdown());
    }

    public ExecutorService diskIO() {
        return diskIO;
    }

    public ExecutorService networkIO() {
        return networkIO;
    }

    public ExecutorService computation() {
        return computation;
    }

    public void shutdown() {
        if (diskIO != null && !diskIO.isShutdown()) {
            diskIO.shutdown();
        }
        if (networkIO != null && !networkIO.isShutdown()) {
            networkIO.shutdown();
        }
        if (computation != null && !computation.isShutdown()) {
            computation.shutdown();
        }
    }

    public void executeDiskIO(Runnable task) {
        try {
            diskIO.execute(task);
        } catch (RejectedExecutionException e) {
            e.printStackTrace();
        }
    }

    public void executeNetworkIO(Runnable task) {
        try {
            networkIO.execute(task);
        } catch (RejectedExecutionException e) {
            e.printStackTrace();
        }
    }

    public void executeComputation(Runnable task) {
        try {
            computation.execute(task);
        } catch (RejectedExecutionException e) {
            e.printStackTrace();
        }
    }
}
