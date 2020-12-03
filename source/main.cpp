// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Feature.h"
#include "SharedCrtResourceManager.h"
#include "config/Config.h"
#include "jobs/JobsFeature.h"
#include "logging/LoggerFactory.h"
#include "tunneling/SecureTunnelingFeature.h"
#include <csignal>
#include <memory>
#include <thread>
#include <vector>

using namespace std;
using namespace Aws::Iot::DeviceClient;
using namespace Aws::Iot::DeviceClient::SecureTunneling;

const char * TAG = "Main.cpp";

vector<Feature*> features;
mutex featuresReadWriteLock;
bool attemptingShutdown;
Config config;

/**
 * Attempts to perform a graceful shutdown of each running feature. If this function is
 * executed more than once, it will terminate immediately.
 */
void shutdown() {
    if(!attemptingShutdown) {
        attemptingShutdown = true;

        featuresReadWriteLock.lock();   // LOCK
        // Make a copy of the features vector for thread safety
        vector<Feature*> featuresCopy = features;
        featuresReadWriteLock.unlock(); // UNLOCK

        for(auto & feature : featuresCopy) {
            LOGM_DEBUG(TAG, "Attempting shutdown of %s", feature->get_name().c_str());
            feature->stop();
        }
        LoggerFactory::getLoggerInstance().get()->shutdown();
    } else {
        // terminate program
        LoggerFactory::getLoggerInstance().get()->shutdown();
        exit(0);
    }
}

void handle_feature_stopped(Feature * feature) {
    featuresReadWriteLock.lock(); // LOCK

    for(int i = 0; (unsigned)i < features.size(); i++) {
        if(features.at(i) == feature) {
            // Performing bookkeeping so we know when all features have stopped
            // and the entire program can be shutdown
            features.erase(features.begin() + i);
        }
    }

    const int size = features.size();
    featuresReadWriteLock.unlock(); // UNLOCK

    if(0 == size) {
        LOG_INFO(TAG, "All features have stopped");
        shutdown();
    }
}

/**
 * DefaultClientBaseNotifier represents the default set of behavior we expect
 * to exhibit when receiving events from a feature. We may want to extend this
 * behavior further for particular features or replace it entirely.
 */
class DefaultClientBaseNotifier final : public ClientBaseNotifier {
    void onEvent(Feature* feature, ClientBaseEventNotification notification) {
        switch(notification) {
            case ClientBaseEventNotification::FEATURE_STARTED: {
                LOGM_INFO(TAG, "Client base has been notified that %s has started", feature->get_name().c_str());
                break;
            }
            case ClientBaseEventNotification::FEATURE_STOPPED: {
                LOGM_INFO(TAG, "%s has stopped", feature->get_name().c_str());
                handle_feature_stopped(feature);
                break;
            }
            default: {
                LOGM_WARN(TAG, "DefaultClientBaseNotifier hit default switch case for feature: %s", feature->get_name().c_str());
            }
        }
    }

    void onError(Feature* feature, ClientBaseErrorNotification error, string msg) {
        switch(error) {
            case ClientBaseErrorNotification::SUBSCRIPTION_REJECTED: {
                LOGM_ERROR(TAG, "Subscription rejected: %s", msg.c_str());
                break;
            }
            case ClientBaseErrorNotification::MESSAGE_RECEIVED_AFTER_SHUTDOWN: {
                LOGM_ERROR(TAG, "Received message after feature shutdown: %s", msg.c_str());
            }
            default: {
                LOGM_ERROR(TAG, "DefaultClientBaseNotifier hit default ERROR switch case for feature: ", feature->get_name().c_str());
            }
        }
        #ifdef NDEBUG
            // DC in release mode - we should decide how we want to behave in this scenario
        #else
            // DC in debug mode
            LOG_ERROR(TAG, "*** DC FATAL ERROR: Aborting program due to unrecoverable feature error! ***");
            LoggerFactory::getLoggerInstance()->shutdown();
            abort();
        #endif
    }
};

int main(int argc, char *argv[])
{
    CliArgs cliArgs;
    if (!Config::ParseCliArgs(argc, argv, cliArgs) || !config.init(cliArgs))
    {
        LoggerFactory::getLoggerInstance()->shutdown();
        return 0;
    }

    // Register for listening to interrupt signals
    sigset_t sigset;
    memset(&sigset, 0, sizeof(sigset_t));
    int received_signal;
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, 0);

    // Initialize features
    shared_ptr<DefaultClientBaseNotifier> listener = shared_ptr<DefaultClientBaseNotifier>(new DefaultClientBaseNotifier);
    shared_ptr<SharedCrtResourceManager> resourceManager =
        shared_ptr<SharedCrtResourceManager>(new SharedCrtResourceManager);
    if (!resourceManager.get()->initialize(config.config))
    {
        LOG_ERROR(
            TAG,
            "*** AWS IOT DEVICE CLIENT FATAL ERROR: Failed to initialize the MQTT Client. Please verify your AWS IoT credentials and/or "
            "configuration. ***");
        LoggerFactory::getLoggerInstance()->shutdown();
        abort();
    }

    unique_ptr<JobsFeature> jobs;
    unique_ptr<SecureTunnelingFeature> tunneling;

    featuresReadWriteLock.lock(); // LOCK
    if (config.config.jobs.enabled)
    {
        jobs = unique_ptr<JobsFeature>(new JobsFeature());
        jobs->init(resourceManager, listener, config.config);
        features.push_back(jobs.get());
    }
    if (config.config.tunneling.enabled)
    {
        tunneling = unique_ptr<SecureTunnelingFeature>(new SecureTunnelingFeature());
        tunneling->init(resourceManager, listener, config.config);
        features.push_back(tunneling.get());
    }
    for (auto &feature : features)
    {
        feature->start();
    }
    featuresReadWriteLock.unlock(); // UNLOCK

    // Now allow this thread to sleep until it's interrupted by a signal
    while (true)
    {
        sigwait(&sigset, &received_signal);
        LOGM_INFO(TAG, "Received signal: (%d)", received_signal);
        if (SIGINT == received_signal)
        {
            resourceManager.get()->disconnect();
            shutdown();
        }
    }

    return 0;
}