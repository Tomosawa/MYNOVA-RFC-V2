/*
 * Copyright (c) 2026 Tomosawa
 * https://github.com/Tomosawa/
 * All rights reserved
 */
#pragma once

#include <Arduino.h>
#include <vector>

class BackupManager {
public:
    static bool ensureCardReady();
    static std::vector<String> listBackups();
    static bool exportBackup(String& outPath, String& outError);
    static bool importBackup(const String& fileName, bool restoreSignals,
                             bool restoreSystem, String& outMessage);
    static bool deleteBackup(const String& fileName);

    // Run before application services load NVS or open any RAW slots.
    static bool recoverInterruptedRestore();
};
