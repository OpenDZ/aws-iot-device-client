// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../source/config/Config.h"
#include "gtest/gtest.h"
#include <aws/crt/JsonObject.h>
#include <stdlib.h>

using namespace std;
using namespace Aws::Crt;
using namespace Aws::Iot::DeviceClient;

TEST(Config, AllFeaturesEnabled)
{
    constexpr char jsonString[] = R"(
{
	"endpoint": "endpoint value",
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value",
    "logging": {
        "level": "debug",
        "type": "file",
        "file": "./aws-iot-device-client.log"
    },
    "jobs": {
        "enabled": true
    },
    "tunneling": {
        "enabled": true
    },
    "device-defender": {
        "enabled": true,
        "interval": 300
    }
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();

    PlainConfig config;
    config.LoadFromJson(jsonView);

    ASSERT_TRUE(config.Validate());
    ASSERT_STREQ("endpoint value", config.endpoint->c_str());
    ASSERT_STREQ("cert", config.cert->c_str());
    ASSERT_STREQ("key", config.key->c_str());
    ASSERT_STREQ("root-ca", config.rootCa->c_str());
    ASSERT_STREQ("thing-name value", config.thingName->c_str());
    ASSERT_STREQ("file", config.logConfig.type.c_str());
    ASSERT_STREQ("./aws-iot-device-client.log", config.logConfig.file.c_str());
    ASSERT_EQ(3, config.logConfig.logLevel); // Expect DEBUG log level, which is 3
    ASSERT_TRUE(config.jobs.enabled);
    ASSERT_TRUE(config.tunneling.enabled);
    ASSERT_TRUE(config.deviceDefender.enabled);
    ASSERT_EQ(300, config.deviceDefender.interval);
}

TEST(Config, HappyCaseMinimumConfig)
{
    constexpr char jsonString[] = R"(
{
	"endpoint": "endpoint value",
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value"
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();

    PlainConfig config;
    config.LoadFromJson(jsonView);

    ASSERT_TRUE(config.Validate());
    ASSERT_STREQ("endpoint value", config.endpoint->c_str());
    ASSERT_STREQ("cert", config.cert->c_str());
    ASSERT_STREQ("key", config.key->c_str());
    ASSERT_STREQ("root-ca", config.rootCa->c_str());
    ASSERT_STREQ("thing-name value", config.thingName->c_str());
    ASSERT_TRUE(config.jobs.enabled);
    ASSERT_TRUE(config.tunneling.enabled);
    ASSERT_TRUE(config.deviceDefender.enabled);
    ASSERT_FALSE(config.fleetProvisioning.enabled);
}

TEST(Config, HappyCaseMinimumCli)
{
    CliArgs cliArgs;
    cliArgs[PlainConfig::CLI_ENDPOINT] = "endpoint value";
    cliArgs[PlainConfig::CLI_CERT] = "cert";
    cliArgs[PlainConfig::CLI_KEY] = "key";
    cliArgs[PlainConfig::CLI_ROOT_CA] = "root-ca";
    cliArgs[PlainConfig::CLI_THING_NAME] = "thing-name value";

    PlainConfig config;
    config.LoadFromCliArgs(cliArgs);

    ASSERT_TRUE(config.Validate());
    ASSERT_STREQ("endpoint value", config.endpoint->c_str());
    ASSERT_STREQ("cert", config.cert->c_str());
    ASSERT_STREQ("key", config.key->c_str());
    ASSERT_STREQ("root-ca", config.rootCa->c_str());
    ASSERT_STREQ("thing-name value", config.thingName->c_str());
    ASSERT_TRUE(config.jobs.enabled);
    ASSERT_TRUE(config.tunneling.enabled);
    ASSERT_TRUE(config.deviceDefender.enabled);
    ASSERT_FALSE(config.fleetProvisioning.enabled);
}

TEST(Config, MissingSomeSettings)
{
    constexpr char jsonString[] = R"(
{
    // endpoint is missing
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value"
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();

    PlainConfig config;
    config.LoadFromJson(jsonView);

#if !defined(DISABLE_MQTT)
    // ST_COMPONENT_MODE does not require any settings besides those for Secure Tunneling
    ASSERT_FALSE(config.Validate());
#else
    ASSERT_TRUE(config.Validate());
#endif
}

TEST(Config, SecureTunnelingMinimumConfig)
{
    constexpr char jsonString[] = R"(
{
	"endpoint": "endpoint value",
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value",
    "tunneling": {
        "enabled": true
    }
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();

    PlainConfig config;
    config.LoadFromJson(jsonView);

    ASSERT_TRUE(config.Validate());
    ASSERT_TRUE(config.tunneling.enabled);
    ASSERT_TRUE(config.tunneling.subscribeNotification);
}

TEST(Config, SecureTunnelingCli)
{
    constexpr char jsonString[] = R"(
{
	"endpoint": "endpoint value",
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value",
    "tunneling": {
        "enabled": true
    }
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();

    CliArgs cliArgs;
    cliArgs[PlainConfig::Tunneling::CLI_TUNNELING_REGION] = "region value";
    cliArgs[PlainConfig::Tunneling::CLI_TUNNELING_SERVICE] = "SSH";
    cliArgs[PlainConfig::Tunneling::CLI_TUNNELING_DISABLE_NOTIFICATION] = "";

    setenv("AWSIOT_TUNNEL_ACCESS_TOKEN", "destination_access_token_value", 1);

    PlainConfig config;
    config.LoadFromJson(jsonView);
    config.LoadFromCliArgs(cliArgs);
    config.LoadFromEnvironment();

    ASSERT_TRUE(config.Validate());
    ASSERT_TRUE(config.tunneling.enabled);
    ASSERT_STREQ("destination_access_token_value", config.tunneling.destinationAccessToken->c_str());
    ASSERT_STREQ("region value", config.tunneling.region->c_str());
#if !defined(EXCLUDE_ST)
    // Do not test against ST GetPortFromService if ST code is excluded
    ASSERT_EQ(22, config.tunneling.port.value());
#endif
    ASSERT_FALSE(config.tunneling.subscribeNotification);
}

TEST(Config, SecureTunnelingDisableSubscription)
{
    constexpr char jsonString[] = R"(
{
	"endpoint": "endpoint value",
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value",
    "tunneling": {
        "enabled": true
    }
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();
    CliArgs cliArgs;
    cliArgs[PlainConfig::Tunneling::CLI_TUNNELING_DISABLE_NOTIFICATION] = "";
    cliArgs[PlainConfig::Tunneling::CLI_TUNNELING_REGION] = "region value";
    cliArgs[PlainConfig::Tunneling::CLI_TUNNELING_SERVICE] = "SSH";

    setenv("AWSIOT_TUNNEL_ACCESS_TOKEN", "destination_access_token_value", 1);

    PlainConfig config;
    config.LoadFromJson(jsonView);
    config.LoadFromCliArgs(cliArgs);
    config.LoadFromEnvironment();

    ASSERT_TRUE(config.Validate());
    ASSERT_TRUE(config.tunneling.enabled);
    ASSERT_FALSE(config.tunneling.subscribeNotification);
    ASSERT_STREQ("destination_access_token_value", config.tunneling.destinationAccessToken->c_str());
    ASSERT_STREQ("region value", config.tunneling.region->c_str());
    ASSERT_EQ(22, config.tunneling.port.value());
}

TEST(Config, LoggingConfigurationCLI)
{
    constexpr char jsonString[] = R"(
{
	"endpoint": "endpoint value",
	"cert": "cert",
	"key": "key",
	"root-ca": "root-ca",
	"thing-name": "thing-name value",
    "logging": {
        "level": "DEBUG",
        "type": "STDOUT",
        "file": "old-json-log.log"
    }
})";
    JsonObject jsonObject(jsonString);
    JsonView jsonView = jsonObject.View();

    CliArgs cliArgs;
    cliArgs[PlainConfig::LogConfig::CLI_LOG_LEVEL] = "warn";
    cliArgs[PlainConfig::LogConfig::CLI_LOG_TYPE] = "FILE";
    cliArgs[PlainConfig::LogConfig::CLI_LOG_FILE] = "./client.log";

    PlainConfig config;
    config.LoadFromJson(jsonView);
    config.LoadFromCliArgs(cliArgs);

    ASSERT_EQ(1, config.logConfig.logLevel); // Expect WARN log level, which is 1
    ASSERT_STREQ("file", config.logConfig.type.c_str());
    ASSERT_STREQ("./client.log", config.logConfig.file.c_str());
}
