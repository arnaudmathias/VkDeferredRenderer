cmake_minimum_required(VERSION 3.6)
project(vkrenderer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)

MESSAGE( STATUS "Vulkan_FOUND:         " ${Vulkan_FOUND} )
MESSAGE( STATUS "Vulkan_INCLUDE_DIRS:         " ${Vulkan_INCLUDE_DIRS} )
MESSAGE( STATUS "Vulkan_LIBRARIES:         " ${Vulkan_LIBRARIES} )
include_directories(${Vulkan_INCLUDE_DIRS})

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third-party/glfw)
include_directories(third-party/glfw/include)
include_directories(third-party/glm)
include_directories(third-party/stb)
include_directories(third-party/tinyobjloader)

SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

file(GLOB SOURCE_FILES
	"src/*.hpp"
	"src/*.cpp"
)

add_executable(vkrenderer ${SOURCE_FILES})

target_link_libraries(vkrenderer ${Vulkan_LIBRARIES})
target_link_libraries(vkrenderer glfw ${GLFW_LIBRARIES})
