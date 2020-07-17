-- Global settings
local workspacePath = path.join("build/", _ACTION)  -- e.g. build/vs2019

workspace ("Typhoon-JobSystem")
	configurations { "Debug", "Release" }
	platforms { "x86", "x86_64" }
	language "C++"
	location (workspacePath)
	characterset "MBCS"
	flags   { "MultiProcessorCompile", }
	startproject "UnitTest"
	exceptionhandling "Off"
	defines { "_HAS_EXCEPTIONS=0" }
	cppdialect "c++17"
	rtti "Off"
	buildoptions {	"/permissive-",	}
	system "Windows"

filter "platforms:x86"
	architecture "x86"
	defines { "WIN32", "_WIN32", }
	  
filter "platforms:x86_64"
	architecture "x86_64"
	defines { "WIN64", "_WIN64", }

filter "configurations:Debug*"
	defines { "_DEBUG", "DEBUG", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1", "_ENABLE_EXTENDED_ALIGNED_STORAGE", }
	flags   { "NoManifest", }
	optimize("Off")
	inlining "Default"
	warnings "Extra"
	symbols "Full"
	runtime "Debug"

filter "configurations:Release*"
	defines { "NDEBUG", "_ITERATOR_DEBUG_LEVEL=0", "_SECURE_SCL=0", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1", "_ENABLE_EXTENDED_ALIGNED_STORAGE", }
	flags   { "NoManifest", "LinkTimeOptimization", "NoBufferSecurityCheck", "NoRuntimeChecks", }
	optimize("Full")
	warnings "Extra"
	inlining "Auto"
	symbols "Off"
	runtime "Release"
	omitframepointer "On"

project("JobSystem")
	kind "StaticLib"
	files "src/**.cpp"
	files "src/**.h"
	files "include/**.*"
	includedirs { "src", "include", }

project("UnitTest")
	kind "ConsoleApp"
	links("JobSystem")
	files "tests/**.cpp"
	includedirs { "./", "external", }

project("Example1")
	kind "ConsoleApp"
	files "examples/example1.cpp"
	includedirs { "./", }
	links("JobSystem")

project("Example2")
	kind "ConsoleApp"
	files "examples/example2.cpp"
	includedirs { "./", }
	links("JobSystem")

project("Example3")
	kind "ConsoleApp"
	files "examples/example3.cpp"
	includedirs { "./", }
	links("JobSystem")

project("Example4")
	kind "ConsoleApp"
	files "examples/example4.cpp"
	includedirs { "./", }
	links("JobSystem")

project("Example5")
	kind "ConsoleApp"
	files "examples/example5.cpp"
	includedirs { "./", }
	links("JobSystem")
