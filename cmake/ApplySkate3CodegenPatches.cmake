if(NOT DEFINED SKATE3_SOURCE_DIR)
  message(FATAL_ERROR "SKATE3_SOURCE_DIR is required")
endif()

file(GLOB _skate3_recomp_files
  LIST_DIRECTORIES false
  "${SKATE3_SOURCE_DIR}/generated/skate3_recomp.*.cpp")
if(NOT _skate3_recomp_files)
  message(FATAL_ERROR "No generated Skate 3 recompilation files found")
endif()

function(_skate3_add_include _contents_var _include)
  set(_contents "${${_contents_var}}")
  if(NOT _contents MATCHES "#include \"${_include}\"")
    # Each recompiled source starts by including its own declarations header.
    string(REGEX REPLACE
      "(#include \"skate3_funcs\\.[0-9]+\\.h\"\n)"
      "\\1#include \"${_include}\"\n"
      _contents
      "${_contents}")
  endif()
  set(${_contents_var} "${_contents}" PARENT_SCOPE)
endfunction()

set(_frustum_patched FALSE)
foreach(_file IN LISTS _skate3_recomp_files)
  file(READ "${_file}" _contents)
  # Migrate previously generated sources with the frustum patch at the caller.
  set(_legacy_scope "\tSkate3UltrawideGameFrustumPatchScope skate3_ultrawide_game_frustum_patch_scope(\n\t\tctx, base, ctx.r4.u32);\n")
  if(_contents MATCHES "Skate3UltrawideGameFrustumPatchScope")
    string(FIND "${_contents}" "${_legacy_scope}" _legacy_at)
    if(_legacy_at EQUAL -1)
      message(FATAL_ERROR "Unexpected legacy frustum patch in ${_file}")
    endif()
    string(REPLACE "${_legacy_scope}" "" _contents "${_contents}")
    string(REPLACE "#include \"skate3_ultrawide_guest.h\"\n" "" _contents "${_contents}")
    file(WRITE "${_file}" "${_contents}")
  endif()

  set(_frustum_entry "DEFINE_REX_FUNC(sub_82816A40) {\n\tREX_FUNC_PROLOGUE();")
  string(FIND "${_contents}" "${_frustum_entry}" _frustum_at)
  if(_frustum_at EQUAL -1)
    continue()
  endif()
  set(_frustum_planes "\n\tSkate3GuestCullPlanes cull_planes(ctx, base, ctx.r4.u32);")
  string(FIND "${_contents}" "${_frustum_entry}${_frustum_planes}" _patched_at)
  if(_patched_at EQUAL -1)
    string(SUBSTRING "${_contents}" ${_frustum_at} -1 _frustum_tail)
    string(FIND "${_frustum_tail}" "\n}\n" _frustum_end)
    if(_frustum_end EQUAL -1)
      message(FATAL_ERROR "Could not find end of guest frustum routine in ${_file}")
    endif()
    string(SUBSTRING "${_frustum_tail}" 0 ${_frustum_end} _frustum_original)
    string(REPLACE "${_frustum_entry}" "${_frustum_entry}${_frustum_planes}"
      _frustum_updated "${_frustum_original}")
    # lvx128 reverses the guest bytes, so plane d is lane zero in the host vector.
    set(_plane 0)
    foreach(_reg IN ITEMS 62 61 60 59 58 56)
      string(REGEX MATCH
        "\t// lvx128 v${_reg},r4,r[0-9]+\n\tea = [^\n]+\n\tsimde_mm_store_si128[^\n]+"
        _plane_load "${_frustum_original}")
      if(NOT _plane_load MATCHES "ctx\\.v${_reg}\\.u8.*VectorMaskL")
        message(FATAL_ERROR "Missing guest frustum plane v${_reg} in ${_file}")
      endif()
      string(REPLACE "${_plane_load}"
        "${_plane_load}\n\tctx.v${_reg}.f32[0] = cull_planes.VrPlaneDistance(${_plane}, ctx.v${_reg}.f32[0]);"
        _frustum_updated "${_frustum_updated}")
      math(EXPR _plane "${_plane} + 1")
    endforeach()
    string(REPLACE "${_frustum_original}" "${_frustum_updated}" _contents "${_contents}")
    _skate3_add_include(_contents "skate3_ultrawide_guest.h")
    file(WRITE "${_file}" "${_contents}")
    message(STATUS "Applied guest frustum plane overrides in ${_file}")
  endif()
  set(_frustum_patched TRUE)
endforeach()
if(NOT _frustum_patched)
  message(FATAL_ERROR "Failed to apply Skate 3 generated frustum patch; sub_82816A40 not found")
endif()

set(_fov_patched FALSE)
foreach(_file IN LISTS _skate3_recomp_files)
  file(READ "${_file}" _contents)
  if(_contents MATCHES "Skate3MaybeOverrideProjectionFovRadians")
    set(_fov_patched TRUE)
    break()
  endif()
  if(NOT _contents MATCHES "ctx\\.f27\\.f64 = ctx\\.f1\\.f64;")
    continue()
  endif()
  if(NOT _contents MATCHES "ctx\\.f4\\.f64 = double\\(float\\(ctx\\.f1\\.f64 \\* ctx\\.f0\\.f64\\)\\);")
    continue()
  endif()

  set(_projection_fov_site "ctx.f27.f64 = ctx.f1.f64;")
  set(_projection_fov_patch
"ctx.f1.f64 = double(Skate3MaybeOverrideProjectionFovRadians(float(ctx.f1.f64)));
	ctx.f27.f64 = ctx.f1.f64;")
  string(REPLACE "${_projection_fov_site}" "${_projection_fov_patch}" _contents "${_contents}")
  _skate3_add_include(_contents "skate3_fov.h")
  file(WRITE "${_file}" "${_contents}")
  set(_fov_patched TRUE)
  message(STATUS "Applied Skate 3 generated projection FOV patch in ${_file}")
  break()
endforeach()
if(NOT _fov_patched)
  message(FATAL_ERROR "Failed to apply Skate 3 generated projection FOV patch; projection FOV anchor not found")
endif()

set(_demo_path_movie_patched FALSE)
foreach(_file IN LISTS _skate3_recomp_files)
  file(READ "${_file}" _contents)
  if(_contents MATCHES "ShouldForceIntroMovieComplete")
    set(_demo_path_movie_patched TRUE)
    break()
  endif()

  set(_demo_path_movie_site
"	// bl 0x825d60c8
	ctx.lr = 0x825E05A0;
	sub_825D60C8(ctx, base);")
  if(NOT _contents MATCHES "ctx\\.lr = 0x825E05A0;")
    continue()
  endif()
  if(NOT _contents MATCHES "DEFINE_REX_FUNC\\(sub_825E0510\\)")
    continue()
  endif()
  string(FIND "${_contents}" "${_demo_path_movie_site}" _demo_path_movie_anchor)
  if(_demo_path_movie_anchor EQUAL -1)
    continue()
  endif()

  set(_demo_path_movie_patch
"	if (skate3::demo_path::ShouldForceIntroMovieComplete()) {
		ctx.r3.u64 = 0;
	} else {
		// bl 0x825d60c8
		ctx.lr = 0x825E05A0;
		sub_825D60C8(ctx, base);
	}")
  string(REPLACE "${_demo_path_movie_site}" "${_demo_path_movie_patch}" _contents "${_contents}")
  _skate3_add_include(_contents "skate3_demo_path.h")
  file(WRITE "${_file}" "${_contents}")
  set(_demo_path_movie_patched TRUE)
  message(STATUS "Applied Skate 3 demo path intro movie patch in ${_file}")
  break()
endforeach()
if(NOT _demo_path_movie_patched)
  message(FATAL_ERROR "Failed to apply Skate 3 demo path intro movie patch; FEMoviePlayer::Update anchor not found")
endif()
