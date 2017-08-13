cmake_minimum_required(VERSION 3.6)
project(vkrenderer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)

MESSAGE( STATUS "Vulkan_FOUND:         " ${Vulkan_FOUND} )
MESSAGE( STATUS "Vulkan_INCLUDE_DIRS:         " ${Vulkan_INCLUDE_DIRS} )
MESSAGE( STATUS "Vulkan_LIBRARIES:         " ${Vulkan_LIBRARIES} )
include_directories(${Vulkan_INCLUDE_DIRS})


add_subdirectory(third-party/glfw)
include_directories(third-party/glfw/include)
include_directories(third-party/glm)

SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

file(GLOB SOURCE_FILES
	"src/*.hpp"
	"src/*.cpp"
)

add_executable(vkrenderer ${SOURCE_FILES})

target_link_libraries(vkrenderer ${Vulkan_LIBRARIES})
target_link_libraries(vkrenderer glfw ${GLFW_LIBRARIES})