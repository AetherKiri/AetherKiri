#include "PsbValueReader.h"

#include "tjs.h"

namespace motion::psb {
    std::optional<std::string>
    valueString(const std::shared_ptr<PSB::IPSBValue> &value) {
        if(auto string = std::dynamic_pointer_cast<PSB::PSBString>(value)) {
            return string->value;
        }
        return std::nullopt;
    }

    std::optional<double>
    valueNumber(const std::shared_ptr<PSB::IPSBValue> &value) {
        if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
            switch(number->numberType) {
                case PSB::PSBNumberType::Float:
                    return number->getValue<float>();
                case PSB::PSBNumberType::Double:
                    return number->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(number->getValue<int>());
                case PSB::PSBNumberType::Long:
                default:
                    return static_cast<double>(number->getValue<tjs_int64>());
            }
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            return boolean->value ? 1.0 : 0.0;
        }
        return std::nullopt;
    }

    std::optional<bool>
    valueBool(const std::shared_ptr<PSB::IPSBValue> &value) {
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            return boolean->value;
        }
        if(const auto number = valueNumber(value)) {
            return *number != 0.0;
        }
        return std::nullopt;
    }

    std::optional<std::string> dictionaryString(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys) {
        for(const auto &key : keys) {
            if(const auto value = (*dictionary)[key]) {
                if(const auto result = valueString(value)) {
                    return result;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<double> dictionaryNumber(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys) {
        for(const auto &key : keys) {
            if(const auto value = (*dictionary)[key]) {
                if(const auto result = valueNumber(value)) {
                    return result;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<bool> dictionaryBool(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys) {
        for(const auto &key : keys) {
            if(const auto value = (*dictionary)[key]) {
                if(const auto result = valueBool(value)) {
                    return result;
                }
            }
        }
        return std::nullopt;
    }

    std::shared_ptr<PSB::PSBList> dictionaryList(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys) {
        for(const auto &key : keys) {
            if(auto value = std::dynamic_pointer_cast<PSB::PSBList>(
                   (*dictionary)[key])) {
                return value;
            }
        }
        return nullptr;
    }

    std::shared_ptr<const PSB::PSBDictionary> dictionaryDictionary(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys) {
        for(const auto &key : keys) {
            if(auto value = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                   (*dictionary)[key])) {
                return value;
            }
        }
        return nullptr;
    }

    std::array<double, 2> dictionaryNumberPair(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys,
        std::array<double, 2> fallback) {
        const auto list = dictionaryList(dictionary, keys);
        if(!list) {
            return fallback;
        }
        for(size_t index = 0; index < fallback.size() && index < list->size();
            ++index) {
            if(const auto value = valueNumber((*list)[static_cast<int>(index)])) {
                fallback[index] = *value;
            }
        }
        return fallback;
    }

    std::array<double, 3> valueNumberVector3(
        const std::shared_ptr<PSB::IPSBValue> &value,
        std::array<double, 3> fallback) {
        const auto list = std::dynamic_pointer_cast<PSB::PSBList>(value);
        if(!list) {
            return fallback;
        }
        for(size_t index = 0;
            index < fallback.size() && index < list->size(); ++index) {
            if(const auto number =
                   valueNumber((*list)[static_cast<int>(index)])) {
                fallback[index] = *number;
            }
        }
        return fallback;
    }

    bool dictionaryVector3Pair(
        const std::shared_ptr<const PSB::PSBDictionary> &dictionary,
        const std::vector<std::string> &keys,
        std::array<std::array<double, 3>, 2> &result) {
        const auto list = dictionaryList(dictionary, keys);
        if(!list || list->size() < 2) {
            return false;
        }
        for(size_t index = 0; index < result.size(); ++index) {
            result[index] = valueNumberVector3(
                (*list)[static_cast<int>(index)], result[index]);
        }
        return true;
    }
}
