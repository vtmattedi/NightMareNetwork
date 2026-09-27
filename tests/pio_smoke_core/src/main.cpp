#include <NightMare.h>

#include <ArduinoJson.h>

RuntimeState smokeState;
Config<uint32_t> smokeConfig("smoke_config", 7);

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
