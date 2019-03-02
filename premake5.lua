workspace "MafiaWaterShaderMod"
   configurations { "Release", "Debug" }
   architecture "x86"
   location "build"
   buildoptions {"-std:c++latest"}
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetextension ".asi"
   
   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""} 
   defines { "rsc_FileVersion=\"1.0.0.0\"", "rsc_ProductVersion=\"1.0.0.0\"" }
   defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name}\"", "rsc_OriginalFilename=\"%{prj.name}.dll\"" }
   defines { "rsc_FileDescription=\"Xbox Rain Droplets Plugin\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/MafiaWaterShaderMod\"" }

   includedirs { "source" }
   includedirs { "source/d3d" }
   libdirs { "source/d3d" }
   files { "source/%{prj.name}.cpp" }
   files { "source/resources/Versioninfo.rc" }
   files { "source/resources/resources.rc" }
   files { "external/hooking/Hooking.Patterns.h", "external/hooking/Hooking.Patterns.cpp" }
   includedirs { "source/resources" }
   includedirs { "source/shaders" }
   includedirs { "source/textures" }
   includedirs { "external/hooking" }
   includedirs { "external/injector/include" }
   local dxsdk = os.getenv "DXSDK_DIR"
   if dxsdk then
      includedirs { dxsdk .. "/include" }
      libdirs { dxsdk .. "/lib/x86" }
   end
   
   linkoptions { "/SAFESEH:NO" }
   characterset ("Unicode")
   
   pbcommands = { 
      "setlocal EnableDelayedExpansion",
      --"set \"path=" .. (gamepath) .. "\"",
      "set file=$(TargetPath)",
      "FOR %%i IN (\"%file%\") DO (",
      "set filename=%%~ni",
      "set fileextension=%%~xi",
      "set target=!path!!filename!!fileextension!",
      "if exist \"!target!\" copy /y \"!file!\" \"!target!\"",
      ")" }

   function setpaths (gamepath, exepath, scriptspath)
      scriptspath = scriptspath or "scripts/"
      if (gamepath) then
         cmdcopy = { "set \"path=" .. gamepath .. scriptspath .. "\"" }
         table.insert(cmdcopy, pbcommands)
         postbuildcommands (cmdcopy)
         debugdir (gamepath)
         if (exepath) then
            debugcommand (gamepath .. exepath)
            dir, file = exepath:match'(.*/)(.*)'
            debugdir (gamepath .. (dir or ""))
         end
      end
      targetdir ("data/scripts")
   end
   
   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      staticruntime "On"
	  
project "MafiaWaterShaderMod"
   setpaths("Z:/mafiatest/Mafia AllPatches/", "GameV12.exe")