cmake_minimum_required(VERSION 2.8.2)
project(ati_sensor)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(catkin QUIET COMPONENTS
  roscpp
  std_srvs
  std_msgs
  geometry_msgs
)
if(NOT ${catkin_FOUND})
    message(WARNING "[${PROJECT_NAME}] catkin not found. DISABLING all ROS-related parts.")  
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/config)

if(${catkin_FOUND})
    catkin_package(
        CATKIN_DEPENDS geometry_msgs std_msgs
        INCLUDE_DIRS include
        LIBRARIES ati_sensor
    )
endif()

include_directories(include)

if(${catkin_FOUND})
    include_directories(${catkin_INCLUDE_DIRS})
endif()

add_library(ati_sensor SHARED src/ft_sensor.cpp)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/config)
find_package(Xenomai QUIET)
# Allows to build for Xenomai + RTnet
if(XENOMAI_FOUND OR (DEFINED $ENV{OROCOS_TARGET} AND $ENV{OROCOS_TARGET} EQUAL "xenomai"))

    message("[${PROJECT_NAME}] Building ATI FT/Sensor for Xenomai ${XENOMAI_VERSION} + RTnet")
    message("[${PROJECT_NAME}] Compiling with ${XENOMAI_RTDM_LIBRARIES} and ${XENOMAI_RTDM_INCLUDE_DIRS} and ${XENOMAI_RTDM_CFLAGS}")

    # In Xenomai 3 it contains RTnet
    include_directories(${XENOMAI_RTDM_INCLUDE_DIRS})
    add_definitions(${XENOMAI_RTDM_CFLAGS})

    # In Xenomai 2 we need the external RTnet
    if(${XENOMAI_VERSION_MAJOR} EQUAL 2)
        find_package(RTnet REQUIRED)
        include_directories(${RTNET_INCLUDE_DIRS})
    endif()

    set_target_properties(ati_sensor PROPERTIES COMPILE_FLAGS -DXENOMAI_VERSION_MAJOR=${XENOMAI_VERSION_MAJOR})
else()
    message(WARNING "[${PROJECT_NAME}] Building ATI FT/Sensor WITHOUT Xenomai/RTnet support")
    find_package(LibXml2 REQUIRED)
    include_directories(${LIBXML2_INCLUDE_DIR})
endif()

target_link_libraries(ati_sensor ${LIBXML2_LIBRARIES} ${XENOMAI_RTDM_LIBRARIES})

if(${catkin_FOUND})
    add_executable(ft_sensor_node src/ft_sensor_node.cpp)
    target_link_libraries(ft_sensor_node ati_sensor ${catkin_LIBRARIES})
endif()

add_executable(simple_reader_test test/simple_reader.cpp)
target_link_libraries(simple_reader_test ati_sensor)

add_executable(gauge_bias_tester  test/test_gauge_bias.cpp)
target_link_libraries(gauge_bias_tester ati_sensor)

if(${catkin_FOUND})
    install(TARGETS ati_sensor
            ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
            LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
            RUNTIME DESTINATION ${CATKIN_GLOBAL_BIN_DESTINATION}
    )

    install(TARGETS ft_sensor_node
            RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
    )

    install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
        FILES_MATCHING PATTERN "*.h"
        PATTERN ".svn" EXCLUDE
    )

    install(DIRECTORY launch
        DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
    )
else()
    # Library installation
    install(TARGETS ati_sensor
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

    install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/include/${CMAKE_PROJECT_NAME}"
        FILES_MATCHING PATTERN "*.h"
        PATTERN ".svn" EXCLUDE)

    # Configuration installation
    configure_file("${CMAKE_PROJECT_NAME}-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
    @ONLY)

    install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
    DESTINATION "share/${CMAKE_PROJECT_NAME}")
endif()


