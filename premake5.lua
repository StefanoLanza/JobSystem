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
	buildoptions
	{	
		"/permissive-",
		-- "structure was padded due to __declspec(align())"
		'/wd4324',
		'/wd4458', --declaration of xxx hides class member 
	}
	system "Windows"
	--targetdir (path.join(workspacePath, "%{cfg.shortname}")) -- target directory for output libraries

filter "platforms:x86"
	architecture "x86"
	defines { "WIN32", "_WIN32", }
	  
filter "platforms:x86_64"
	architecture "x86_64"
	defines { "WIN64", "_WIN64", }

filter "configurations:Debug*"
	defines { "_DEBUG", "DEBUG", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1", "_ENABLE_EXTENDED_ALIGNED_STORAGE", }
	flags   { "NoManifest", }
	callingconvention("VectorCall")
	optimize("Off")
	inlining "Default"
	warnings "Extra"
	symbols "Full"
	runtime "Debug"

filter "configurations:Release*"
	defines { "NDEBUG", "_ITERATOR_DEBUG_LEVEL=0", "_SECURE_SCL=0", "FINAL", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1", "_ENABLE_EXTENDED_ALIGNED_STORAGE", }
	flags   { "NoManifest", "LinkTimeOptimization", "NoBufferSecurityCheck", "NoRuntimeChecks", }
	callingconvention("VectorCall")
	optimize("Full")
	warnings "Extra"
	inlining "Auto"
	symbols "Off"
	runtime "Release"
	omitframepointer "On"
	buildoptions
	{	
		"/Ot", -- favor fast code
		"/Oi", -- enable intrinsic functions
		'/fp:except-', -- disable floating point exceptions
		'/Ob2', -- inline any suitable
	}


project("JobSystem")
	kind "StaticLib"
	files "src/**.cpp"
	files "src/**.h"
	includedirs { "src", }

project("UnitTest")
	kind "ConsoleApp"
	links("JobSystem")
	files "tests/**.cpp"
	includedirs { "./", }
