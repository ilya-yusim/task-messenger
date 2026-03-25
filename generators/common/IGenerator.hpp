/**
 * \file IGenerator.hpp
 * \brief Optional interface for generator algorithms.
 *
 * Provides a documented contract for generator implementations.
 * Power users implement this interface; the common TaskGenerator
 * is a convenience base class that implements it.
 *
 * This is a convention, not a requirement. Generators can still
 * write raw main() and use DispatcherApp directly.
 */
#pragma once

class DispatcherApp;

/**
 * \brief Optional contract for generator algorithms.
 *
 * Implementors define their initialization, main loop, and shutdown behavior.
 * The generator executable creates a DispatcherApp, constructs an IGenerator,
 * and drives the lifecycle:
 *
 * \code
 * DispatcherApp app;
 * if (int rc = app.start(argc, argv); rc != 0) return (rc == 1) ? 0 : rc;
 * MyGenerator gen;
 * if (!gen.initialize(app)) return 1;
 * int result = gen.run(app);
 * gen.on_shutdown();
 * app.stop();
 * return result;
 * \endcode
 */
class IGenerator {
public:
    virtual ~IGenerator() = default;

    /**
     * \brief One-time setup after the dispatcher is started.
     * \param app The running dispatcher (transport is live, logger is available).
     * \return true if initialization succeeded, false to abort.
     *
     * Use this to configure generator-specific state, validate preconditions,
     * or log startup information. The dispatcher is fully operational when
     * this is called.
     */
    virtual bool initialize(DispatcherApp& app) = 0;

    /**
     * \brief The generator's main loop.
     * \param app The running dispatcher for submitting tasks.
     * \return Process exit code (0 = success).
     *
     * This method owns the control flow. It decides when to submit tasks,
     * how to wait for results, and when to exit. The method should return
     * when work is complete or when app.shutdown_requested() becomes true.
     */
    virtual int run(DispatcherApp& app) = 0;

    /**
     * \brief Called during the shutdown path, after run() returns.
     *
     * Invoked by run_generator() after the main loop finishes and before
     * the dispatcher is stopped.  Use this to release resources, join
     * background threads, or stop internal dispatch loops.
     *
     * Must be safe to call even if run() returned early or was never
     * called.  Default implementation does nothing (generators can
     * perform all cleanup in run() instead).
     */
    virtual void on_shutdown() {}
};
