#pragma once

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "main.hpp"

class Hooks {
   private:
    static inline std::vector<void (*)()> installFuncs;

   public:
    static inline void AddInstallFunc(void (*installFunc)()) { installFuncs.push_back(installFunc); }

    static inline void Install() {
        for (auto& func : installFuncs)
            func();
    }
};
