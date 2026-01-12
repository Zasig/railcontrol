cmake_minimum_required(VERSION 3.14)
project(MyTrainControl VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Thread-Bibliothek benötigt
find_package(Threads REQUIRED)

# Header-only Bibliothek
add_library(ccschnitte INTERFACE)
target_include_directories(ccschnitte INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ccschnitte INTERFACE Threads::Threads)

# Beispiel-Programm
add_executable(mytraincontrol src/main.cpp)
target_link_libraries(mytraincontrol PRIVATE ccschnitte)

# Installation
install(TARGETS mytraincontrol DESTINATION bin)
install(FILES
    src/CcSchnitte.h
    src/DataTypes.h
    src/Logger.h
    src/Serial.h
    src/Utils.h
    DESTINATION include/mytraincontrol
)

