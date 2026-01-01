// VMapUnity.cpp
// This file compiles the VMap and G3D libraries.

#pragma warning(disable : 4244) // conversion from 'double' to 'float'
#pragma warning(disable : 4267) // conversion from 'size_t' to 'uint32'
#pragma warning(disable : 4305) // truncation
#pragma warning(disable : 4005) // macro redefinition
#pragma warning(disable : 4996) // unsafe functions
#pragma warning(disable : 4477) // sprintf format (WCHAR vs char)

#define isNewline isNewline_BinaryInput
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/BinaryInput.cpp"
#undef isNewline

// 2. INCLUDE G3D FILES (Full Paths)
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/BinaryInput.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/BinaryOutput.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/FileSystem.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/TextInput.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/TextOutput.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Vector3.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Matrix3.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Ray.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Triangle.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/AABox.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Plane.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/System.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/format.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/stringutils.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Any.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/prompt.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/RegistryUtil.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/dep/g3dlite/source/Log.cpp"

// 3. INCLUDE VMAP CORE FILES (Full Paths)
#include "C:/Users/A/Downloads/SkyFire_548/Core/src/server/collision/BoundingIntervalHierarchy.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/src/server/collision/Maps/MapTree.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/src/server/collision/Maps/TileAssembler.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/src/server/collision/Models/ModelInstance.cpp"
#include "C:/Users/A/Downloads/SkyFire_548/Core/src/server/collision/Models/WorldModel.cpp"

// 4. INCLUDE YOUR LOCAL MANAGER (Relative Path is fine here)
#include "VMapManager2.cpp"