#include <NightMare.h>

#include <ArduinoJson.h>

RuntimeState smokeState;
Config<uint32_t> smokeConfig("smoke_config", 7);

namespace
{
int configCallbackValue = 0;

bool acceptPositiveConfig(Config<int> &, const int &requested)
{
    configCallbackValue = requested;
    return requested > 0;
}

bool rejectGlobalConfig(const String &key, const String &)
{
    return key != "persist_global_reject";
}
}

static_assert(NetCodec<TimeType>::Type == NetValueType::TIME,
              "TimeType must have a first-class wire type");
static_assert(NetCodec<ColourType>::Type == NetValueType::COLOUR,
              "ColourType must have a first-class wire type");
static_assert(static_cast<uint8_t>(NetValueType::STRUCT) == 4 &&
                  static_cast<uint8_t>(NetValueType::TIME) == 5 &&
                  static_cast<uint8_t>(NetValueType::COLOUR) == 6,
              "semantic wire types must append without renumbering existing values");

void setup()
{
    gDeviceIdentity.begin();
    smokeState.setFlag("booted", true);
    smokeState.setFlag("clock_valid", NightMare::Time::valid());

    const String configPrefix = "_config:";
    const String defaultKey = configPrefix + "persist_default";
    const String restoredKey = configPrefix + "persist_restored";
    const String invalidKey = configPrefix + "persist_invalid";
    const String localKey = configPrefix + "persist_local";
    const String commandKey = configPrefix + "persist_command";
    const String onWriteRejectKey = configPrefix + "persist_onwrite_reject";
    const String globalRejectKey = configPrefix + "persist_global_reject";
    const String timeKey = configPrefix + "persist_time";
    const String colourKey = configPrefix + "persist_colour";
    const String failureKey = configPrefix + "persist_failure";
    const String persistenceKeys[] = {
        defaultKey, restoredKey, invalidKey, localKey, commandKey,
        onWriteRejectKey, globalRejectKey, timeKey, colourKey, failureKey};
    for (const String &key : persistenceKeys)
        PersistentSettings.remove(key);
    PersistentSettings.set(restoredKey, "23");
    PersistentSettings.set(invalidKey, "not-an-integer");
    PersistentSettings.set(timeKey, "21:45:30");
    PersistentSettings.set(colourKey, "16909060");

    bool configPersistence = false;
    {
        Config<int> firstBoot("persist_default", 5);
        Config<int> restored("persist_restored", 7);
        Config<int> invalid("persist_invalid", 11);
        Config<int> local("persist_local", 3);
        Config<int> command("persist_command", 4);
        Config<int> onWriteRejected("persist_onwrite_reject", 6);
        Config<int> globalRejected("persist_global_reject", 8);
        Config<TimeType> persistedTime("persist_time", TimeType(1, 2, 3));
        Config<ColourType> persistedColour("persist_colour", ColourType());
        onWriteRejected.onWrite = acceptPositiveConfig;

        const bool restoredAll = configManager().restore() && configManager().restore();
        const bool bootValues = firstBoot.value() == 5 && restored.value() == 23 &&
                                invalid.value() == 11 &&
                                persistedTime.value() == TimeType(21, 45, 30) &&
                                persistedColour.value() == ColourType(0x01020304UL) &&
                                PersistentSettings.get(defaultKey) == "5" &&
                                PersistentSettings.get(invalidKey) == "11";
        const bool localPersisted = local.set(31) && local.value() == 31 &&
                                    PersistentSettings.get(localKey) == "31";
        const bool commandPersisted =
            configManager().handle("set persist_command 41") == "OK" &&
            command.value() == 41 && PersistentSettings.get(commandKey) == "41";
        const bool onWriteRejectedCleanly =
            configManager().handle("set persist_onwrite_reject -1") ==
                "ERROR: change rejected" &&
            onWriteRejected.value() == 6 && PersistentSettings.get(onWriteRejectKey) == "6";
        configManager().setChangeHandler(rejectGlobalConfig);
        const bool globalRejectedCleanly =
            configManager().handle("set persist_global_reject 9") ==
                "ERROR: change rejected" &&
            globalRejected.value() == 8 && PersistentSettings.get(globalRejectKey) == "8";
        configManager().setChangeHandler(nullptr);

        Config<int> persistenceFailure("persist_failure", 12);
        for (size_t i = 0;
             i < RuntimeState::MaxEntries &&
             PersistentSettings.size() < RuntimeState::MaxEntries;
             ++i)
            if (!PersistentSettings.RuntimeState::set(
                    String("_config_test_fill:") + String(i), "x"))
                break;
        const bool failurePreservedRuntime = !persistenceFailure.set(99) &&
                                             persistenceFailure.value() == 12 &&
                                             !PersistentSettings.exists(failureKey);
        PersistentSettings.load();

        configPersistence = restoredAll && bootValues && localPersisted &&
                            commandPersisted && onWriteRejectedCleanly &&
                            globalRejectedCleanly && failurePreservedRuntime;
    }
    smokeState.setFlag("config_persistence", configPersistence);

    const size_t staticCount = configManager().count();
    const bool staticAutoBound = staticCount == 1 &&
                                 configManager().handle("get smoke_config") == "7" &&
                                 !configManager().bind(&smokeConfig);
    bool scopedLifecycle = false;
    {
        Config<int> scoped("scoped_config", 11);
        const bool autoBound = configManager().count() == staticCount + 1 &&
                               configManager().handle("get scoped_config") == "11";
        const bool manuallyUnbound = configManager().unbind(&scoped) &&
                                     configManager().handle("get scoped_config") ==
                                         "ERROR: unknown config";
        const bool manuallyRebound = configManager().bind(&scoped) &&
                                     !configManager().bind(&scoped);
        scopedLifecycle = autoBound && manuallyUnbound && manuallyRebound;
    }
    const bool scopedAutoUnbound = configManager().count() == staticCount &&
                                   configManager().handle("get scoped_config") ==
                                       "ERROR: unknown config";
    Config<int> invalidName("invalid name", 13);
    const bool failedAutoBindStaysLocal = invalidName.value() == 13 &&
                                         configManager().count() == staticCount;
    smokeState.setFlag("config_lifecycle", staticAutoBound && scopedLifecycle &&
                                               scopedAutoUnbound && failedAutoBindStaysLocal);

    Config<int> callbackConfig("callback_config", 5);
    callbackConfig.onWrite = acceptPositiveConfig;
    const bool managerCallbackAccepted =
        configManager().handle("set callback_config 9") == "OK" &&
        callbackConfig.value() == 9 && configCallbackValue == 9;
    const bool managerCallbackRejected =
        configManager().handle("set callback_config -2") == "ERROR: change rejected" &&
        callbackConfig.value() == 9 && configCallbackValue == -2;
    configCallbackValue = 0;
    callbackConfig.set(12);
    const bool localSetBypassesCallback = callbackConfig.value() == 12 &&
                                          configCallbackValue == 0;
    JsonDocument configList;
    const bool configListDecoded =
        !deserializeJson(configList, configManager().handle("list"));
    bool currentStateListed = false;
    for (JsonObjectConst item : configList.as<JsonArrayConst>())
        if (item["name"].as<String>() == "callback_config")
            currentStateListed = item["type"].as<String>() == "integer" &&
                                 item["value"].as<String>() == "12" &&
                                 !item["require_reboot"].as<bool>();
    smokeState.setFlag("config_on_write", managerCallbackAccepted &&
                                              managerCallbackRejected &&
                                              localSetBypassesCallback &&
                                              configListDecoded && currentStateListed);

    TimeType decodedTime(1, 2, 3);
    const bool timeDefaults = TimeType() == TimeType(0, 0, 0) &&
                              TimeType().toString() == "00:00:00";
    const bool timeForms = decodeTime("08:30:45", decodedTime) &&
                           decodedTime == TimeType(8, 30, 45) &&
                           decodeTime("08:30", decodedTime) &&
                           decodedTime == TimeType(8, 30) &&
                           decodeTime("083045", decodedTime) &&
                           decodedTime == TimeType(8, 30, 45) &&
                           decodeTime("0830", decodedTime) &&
                           decodedTime == TimeType(8, 30) &&
                           decodeTime("8:3:5", decodedTime) &&
                           decodedTime == TimeType(8, 3, 5) &&
                           decodeTime("8:3", decodedTime) &&
                           decodedTime == TimeType(8, 3);
    const TimeType preservedTime = decodedTime;
    const bool invalidTimesRejected = !decodeTime("24:00", decodedTime) &&
                                      !decodeTime("12:60", decodedTime) &&
                                      !decodeTime("12:00:60", decodedTime) &&
                                      decodedTime == preservedTime;
    TimeType codecTime;
    const bool timeCodec = NetCodec<TimeType>::encode(TimeType(9, 4, 2)) == "09:04:02" &&
                           NetCodec<TimeType>::decode("090402", codecTime) &&
                           codecTime == TimeType(9, 4, 2) &&
                           codecTime != TimeType(9, 4, 3);
    smokeState.setFlag("time_type", timeDefaults && timeForms && invalidTimesRejected &&
                                       timeCodec);

    const ColourType red(255, 0, 0);
    const NightMare::RGB redRgb = red.toRGB();
    const NightMare::RGBA redRgba = red.toRGBA();
    const NightMare::HSV redHsv = red.toHSV();
    ColourType decodedColour;
    const bool colourDefaults = ColourType().rawValue() == 0;
    const bool colourConversions = red.rawValue() == 0xff0000ffUL &&
                                   redRgb.r == 255 && redRgb.g == 0 && redRgb.b == 0 &&
                                   redRgba.r == 255 && redRgba.g == 0 && redRgba.b == 0 &&
                                   redRgba.a == 255 && redHsv.h == 0 && redHsv.s == 255 &&
                                   redHsv.v == 255 &&
                                   ColourType::fromHSV(redHsv) == red;
    const bool colourForms = decodeColour("#FF0000", decodedColour) &&
                             decodedColour == red &&
                             decodeColour("255", decodedColour) &&
                             decodedColour == ColourType(0, 0, 255) &&
                             decodeColour("0xFF0000", decodedColour) &&
                             decodedColour == red &&
                             decodeColour("0xFF000080", decodedColour) &&
                             decodedColour == ColourType(255, 0, 0, 128) &&
                             decodeColour("#FF000080", decodedColour) &&
                             decodedColour == ColourType(255, 0, 0, 128) &&
                             decodeColour("rgb(1, 2, 3)", decodedColour) &&
                             decodedColour == ColourType(1, 2, 3) &&
                             decodeColour("rgba(1,2,3,4)", decodedColour) &&
                             decodedColour == ColourType(1, 2, 3, 4) &&
                             decodeColour("hsv(0,255,255)", decodedColour) &&
                             decodedColour == red;
    const String encodedRed = NetCodec<ColourType>::encode(red);
    ColourType codecColour;
    const bool colourCodec = encodedRed == "4278190335" &&
                             NetCodec<ColourType>::decode(encodedRed, codecColour) &&
                             codecColour == red && codecColour != ColourType();
    const ColourType preservedColour = codecColour;
    const bool invalidColoursRejected = !decodeColour("rgb(256,0,0)", codecColour) &&
                                        !decodeColour("#12345", codecColour) &&
                                        codecColour == preservedColour;
    smokeState.setFlag("colour_type", colourDefaults && colourConversions && colourForms &&
                                         colourCodec && invalidColoursRejected);

    Config<TimeType> timeConfig("smoke_time", TimeType(8, 30));
    Config<ColourType> colourConfig("smoke_colour", red);
    const bool configValues = configManager().handle("get smoke_time") == "08:30:00" &&
                              configManager().handle("set smoke_time 9:45") == "OK" &&
                              timeConfig.value() == TimeType(9, 45) &&
                              configManager().handle("get smoke_colour") == encodedRed &&
                              configManager().handle("set smoke_colour #00FF00") == "OK" &&
                              colourConfig.value() == ColourType(0, 255, 0);
    uint8_t manifestBytes[256];
    size_t manifestLength = 0;
    JsonDocument configManifest;
    bool configManifestTypes =
        configManager().buildManifestMsgPack(manifestBytes, sizeof(manifestBytes),
                                             manifestLength) &&
        !deserializeMsgPack(configManifest, manifestBytes, manifestLength);
    bool sawTime = false;
    bool sawColour = false;
    for (JsonArrayConst item : configManifest[2].as<JsonArrayConst>())
    {
        if (item[0].as<String>() == "smoke_time")
            sawTime = item[1].as<uint8_t>() == static_cast<uint8_t>(NetValueType::TIME);
        if (item[0].as<String>() == "smoke_colour")
            sawColour = item[1].as<uint8_t>() == static_cast<uint8_t>(NetValueType::COLOUR);
    }
    smokeState.setFlag("config_custom_types", configValues && configManifestTypes && sawTime &&
                                               sawColour);
}

void loop() {}
