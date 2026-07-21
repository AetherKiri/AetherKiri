#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "psbfile/PSBFile.h"

namespace motion::psb {
    std::optional<std::string>
    valueString(const std::shared_ptr<PSB::IPSBValue> &value);

    std::optional<double>
    valueNumber(const std::shared_ptr<PSB::IPSBValue> &value);

    std::optional<bool>
    valueBool(const std::shared_ptr<PSB::IPSBValue> &value);

    std::optional<std::string> dictionaryString(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys);

    std::optional<double> dictionaryNumber(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys);

    std::optional<bool> dictionaryBool(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys);

    std::shared_ptr<PSB::PSBList> dictionaryList(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys);

    std::shared_ptr<const PSB::PSBDictionary> dictionaryDictionary(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys);

    std::array<double, 2> dictionaryNumberPair(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys,
        std::array<double, 2> fallback = {0.0, 0.0});

    std::array<double, 3> valueNumberVector3(
        const std::shared_ptr<PSB::IPSBValue> &value,
        std::array<double, 3> fallback = {0.0, 0.0, 0.0});

    bool dictionaryVector3Pair(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys,
        std::array<std::array<double, 3>, 2> &result);
}
