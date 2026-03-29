//
// Created by kemptonburton on 11/14/2025.
//

#ifndef BASEMODULE_H
#define BASEMODULE_H

#include <yaml-cpp/yaml.h>
#include "sdk_api.h"

namespace DARTWIC::Modules {

    class BaseModule {
    public:
        virtual ~BaseModule() = default;
        BaseModule(YAML::Node cfg, API::SDK_API* drtw) : config(cfg), dartwic(drtw) {}

        virtual void onRegistryLoaded() {}

        template <typename T>
        T getConfig(const std::string& key, const T& default_value = T()) const {
            if (config && config[key]) {
                return config[key].as<T>();
            }
            return default_value;
        }

        template <typename T>
        T getParameter(const std::string& key, const T& default_value = T()) const {
            if (config && config["parameters"][key]) {
                return config["parameters"][key].as<T>();
            }
            return default_value;
        }

        void setRegistryName(const std::string& registry_name_value) {
            registry_name = registry_name_value;
        }

        const std::string& getRegistryName() const {
            return registry_name;
        }

        YAML::Node config;
        API::SDK_API* dartwic = nullptr;

    protected:
        std::string registry_name;
    };
}

#endif //BASEMODULE_H
