# Pinned, shipped third-party sources. All hashes cover the exact vendored bytes.
set(freetype_VERSION "2.14.1")
set(freetype_COMMIT "526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b")
set(freetype_SHA256 "d24f53b60034dc5c48e90d122d432a8f04a05b9e0a98c8d8e49b4a759207ce2e")
set(freetype_URL "https://codeload.github.com/freetype/freetype/tar.gz/${freetype_COMMIT}")
set(freetype_CREDIT_YEAR "2025")
set(freetype_LICENSE_FILES docs/FTL.TXT src/bdf/README src/pcf/README src/gzip/zlib.h)
set(harfbuzz_VERSION "14.6.0")
set(harfbuzz_COMMIT "c7a7457b7385f33178e8cf87615ca077a810bbe7")
set(harfbuzz_SHA256 "10dcea60bd85ff219490d55525630650ac8c57044c4efdc878162f286451775b")
set(harfbuzz_LICENSE_FILES COPYING src/ms-use/COPYING)
set(harfbuzz_URL "https://codeload.github.com/harfbuzz/harfbuzz/tar.gz/${harfbuzz_COMMIT}")
set(glcorearb_COMMIT "6af574a14089ccfee87efe230ebcdd8742859813")
set(glcorearb_SHA256 "65fae555a8b3b5709099837001e0c14228b3e45651e2d0fa2ad22649833921e1")
set(glcorearb_URL "https://github.com/KhronosGroup/OpenGL-Registry/blob/${glcorearb_COMMIT}/api/GL/glcorearb.h")
set(khrplatform_COMMIT "db3425b8246136faccb5e2782b5694960bd6edf1")
set(khrplatform_SHA256 "7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83")
set(khrplatform_URL "https://github.com/KhronosGroup/EGL-Registry/blob/${khrplatform_COMMIT}/api/KHR/khrplatform.h")
# Unicode Character Database file the vertical-orientation table (src/vertical_orientation_data.inc) is generated from.
set(unicode_vo_VERSION "18.0.0")
set(unicode_vo_SHA256 "0803e09669d7aa7137de678e55b848bd418c6632694fa1b05f0b40941103c748")
set(unicode_vo_URL "https://www.unicode.org/Public/${unicode_vo_VERSION}/ucd/VerticalOrientation.txt")
set(unicode_license_URL "https://www.unicode.org/license.txt")
set(unicode_license_SHA256 "e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96")
set(vulkan_VERSION "vulkan-sdk-1.4.363.0")
set(vulkan_COMMIT "6802bb4733b63ed5efd3adb308a6c885ef180ea1")
set(vulkan_URL "https://github.com/KhronosGroup/Vulkan-Headers/tree/${vulkan_COMMIT}")
set(vulkan_files
  "vk_video/vulkan_video_codec_av1std.h|c75c1d324b97d247aef0008024bc7f3adb98c741ab7f66c882ec38fdacc7ee33"
  "vk_video/vulkan_video_codec_av1std_decode.h|7e3a1ce177c12546d410f3179ce1b81f2da7a8eba4f525b29cd563ab4099c0e5"
  "vk_video/vulkan_video_codec_av1std_encode.h|8d166b4543260a38347860443b1a59c7a5e86cdb0d0facaf4c704f667de030e3"
  "vk_video/vulkan_video_codec_h264std.h|fded484cef9f90fbbd089e0647268f36e5f4292179fbc5428a2c9e8d7709bbe9"
  "vk_video/vulkan_video_codec_h264std_decode.h|37b970c3d80536ad1ac074cfe19b58ee03c3075378e02179e1dc5e4351266821"
  "vk_video/vulkan_video_codec_h264std_encode.h|227e092b53c4e7ca1a948ed021704511c1ed16040cd6188ff6703e5ae66db64d"
  "vk_video/vulkan_video_codec_h265std.h|0b81f8986ada015ef2e127449eff9d9634899bb59a0a9277a4054e9ec4416c12"
  "vk_video/vulkan_video_codec_h265std_decode.h|879a0dd370a1b1ad184638906c52bdfe7d80d639fbc5a5baa8b02d7c5b60b147"
  "vk_video/vulkan_video_codec_h265std_encode.h|abb3e72af22e4e0a3dbe5dff7be1b275949388809fb3987830341e8f495ea1c7"
  "vk_video/vulkan_video_codec_vp9std.h|0a47125865376a3fe7014b69ff6db9d04e30ebf8c4d15664f1d894649ad5c09d"
  "vk_video/vulkan_video_codec_vp9std_decode.h|1ceb1a8d0e3370e508cf688a6e57dc314cd82186b60f3cef420ea4b1b483865d"
  "vk_video/vulkan_video_codecs_common.h|d2e7caa396c521d03d491a269572b1d31b925b5127d22fda14199951ebae89f8"
  "vulkan/LICENSES/MIT.txt|1ca3502222d967f3be5751c55f6b7ee735b5383909c3b501495f54b216dbf227"
  "vulkan/vk_platform.h|a2cd9085c66776845d2524c3b0e73ccf4827ba046aa6f676c4b8ddbed47d6552"
  "vulkan/vulkan.h|096f50152d0b298d8df84a561a37d8190c8a60af34dff0e2cc8328b24491a640"
  "vulkan/vulkan_core.h|2f64fd6c7c3f342b0e44f039f1c359b7f0e6cfcb71a7dca1cc5156282faa3b51"
  "vulkan/vulkan_wayland.h|6c4146149d45bcbb5a22c7540feaa396ee9a49f077737c7a343c62de5199fbc7"
  "vulkan/vulkan_win32.h|72f0b6de71287d3b04d12235e4f2fef8f343126f85be4963c516d31d5bb4c099"
  "vulkan/vulkan_xcb.h|3d49f5eb52090e72e1cf7cde545088225ac524a2ff1d1f35737e7d944474c1b7"
)
function(verify_third_party root)
  foreach(entry IN LISTS vulkan_files)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 path)
    list(GET parts 1 expected)
    file(SHA256 "${root}/third_party/${path}" actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "${path} source checksum mismatch")
    endif()
  endforeach()
  foreach(entry "freetype|freetype.tar.gz" "harfbuzz|harfbuzz.tar.gz" "glcorearb|glcorearb.h" "khrplatform|KHR/khrplatform.h"
                "unicode_vo|unicode/VerticalOrientation-${unicode_vo_VERSION}.txt" "unicode_license|unicode/LICENSE-UNICODE-V3.txt")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 path)
    file(SHA256 "${root}/third_party/${path}" actual)
    if(NOT actual STREQUAL "${${name}_SHA256}")
      message(FATAL_ERROR "${path} source checksum mismatch: expected ${${name}_SHA256}, got ${actual}")
    endif()
  endforeach()
endfunction()
