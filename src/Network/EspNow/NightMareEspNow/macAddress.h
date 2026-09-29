#pragma once
#include <Arduino.h>//Only while not moving over to espidf

struct MacAddress {
    static constexpr size_t Length = 6;
    uint8_t bytes[Length] = {};

    MacAddress() = default;
    explicit MacAddress(const uint8_t *raw);
    bool operator==(const MacAddress &other) const;
    MacAddress operator=(const uint8_t *other);
    String toString() const;
}__attribute__((packed));
