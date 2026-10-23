#pragma once
// Necessaire pour esp_wifi_set_ps() appele depuis un lambda YAML.
// ESPHome n'inclut pas esp_wifi.h dans l'unite de compilation de main.cpp.
#include <esp_wifi.h>
