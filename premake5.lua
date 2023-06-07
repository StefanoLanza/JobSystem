-- Options
newoption {
	trigger     = "with-tests",
	description = "Build the unit test application",
}

newoption {
	trigger     = "with-examples",
	description = "Build the examples",
}

-- Global settings
local workspacePath = path.join("build/", _ACTION)  -- e.g. build/vs2022

-- Filters
local filter_vs = "action:vs*"
local filter_xcode = "action:xcode*"
local filter_x86 = "platforms:x86"
local filter_x64 = "platforms:x86_64"
local filter_debug =  "configurations:Debug*"
local filter_release =  "configurations:Release*"

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

filter { filter_vs }
	buildoptions { "/permissive-", }
	system "Windows"
	defines { "_ENABLE_EXTENDED_ALIGNED_STORAGE", }
	-- systemversion "10.0.17134.0"

filter { filter_xcode }
	system "macosx"
	systemversion("10.12") -- MACOSX_DEPLOYMENT_TARGET

filter { "toolset:gcc" }
    -- https://stackoverflow.com/questions/39236917/using-gccs-link-time-optimization-with-static-linked-libraries
    buildoptions { "-ffat-lto-objects" }
    linkoptions { "-pthread" }

filter {  "toolset:clang" }
    linkoptions { "-pthread" }

filter { filter_x86 }
	architecture "x86"
	  
filter { filter_x64 }
	architecture "x86_64"

filter { filter_vs, filter_x86, }
	defines { "WIN32", "_WIN32", }

filter { filter_vs, filter_x64, }
	defines { "WIN64", "_WIN64", }

filter { filter_vs, filter_debug, }
	defines { "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1",  }

filter { filter_vs, filter_release, }
	defines { "_ITERATOR_DEBUG_LEVEL=0", "_SECURE_SCL=0", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1",  }

filter { filter_debug }
	defines { "_DEBUG", "DEBUG", }
	flags   { "NoManifest", }
	optimize("Off")
	inlining "Default"
	warnings "Extra"
	symbols "Full"
	runtime "Debug"

filter { filter_release }
	defines { "NDEBUG", }
	flags   { "NoManifest", "LinkTimeOptimization", "NoBufferSecurityCheck", "NoRuntimeChecks", }
	optimize("Full")
	inlining "Auto"
	warnings "Extra"
	symbols "Off"
	runtime "Release"

project("JobSystem")
	kind "StaticLib"
	files "src/**.cpp"
	files "src/**.h"
	files "include/jobSystem/**.*"
	includedirs { "src", "include/jobSystem", }

if _OPTIONS["with-tests"] then

project("UnitTest")
	kind "ConsoleApp"
	links("JobSystem")
	files "tests/**.cpp"
	externalincludedirs { "./", "external", "include",}

end

if _OPTIONS["with-examples"] then

project("Example1")
	kind "ConsoleApp"
	files "examples/example1.cpp"
	externalincludedirs { "./", "include", }
	links("JobSystem")

project("Example2")
	kind "ConsoleApp"
	files "examples/example2.cpp"
	externalincludedirs { "./", "include", }
	links("JobSystem")

project("Example3")
	kind "ConsoleApp"
	files "examples/example3.cpp"
	externalincludedirs { "./", "include", }
	links("JobSystem")

project("Example4")
	kind "ConsoleApp"
	files "examples/example4.cpp"
	externalincludedirs { "./", "include", }
	links("JobSystem")

project("Example5")
	kind "ConsoleApp"
	files "examples/example5.cpp"
	externalincludedirs { "./", "include", }
	links("JobSystem")

end

