# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os

from gpu_tests.gpu_test_expectations import GpuTestExpectations

# See the GpuTestExpectations class for documentation.

class WebGLConformanceExpectations(GpuTestExpectations):
  def __init__(self, conformance_path):
    self.conformance_path = conformance_path
    super(WebGLConformanceExpectations, self).__init__()

  def Fail(self, pattern, condition=None, bug=None):
    self.CheckPatternIsValid(pattern)
    GpuTestExpectations.Fail(self, pattern, condition, bug)

  def Skip(self, pattern, condition=None, bug=None):
    self.CheckPatternIsValid(pattern)
    GpuTestExpectations.Skip(self, pattern, condition, bug)

  def CheckPatternIsValid(self, pattern):
    # Look for basic wildcards.
    if not '*' in pattern:
      full_path = os.path.normpath(os.path.join(self.conformance_path, pattern))
      if not os.path.exists(full_path):
        raise Exception('The WebGL conformance test path specified in' +
          'expectation does not exist: ' + full_path)

  def SetExpectations(self):
    # Fails on all platforms
    self.Fail('deqp/data/gles2/shaders/functions.html',
        bug=478572)
    self.Fail('deqp/data/gles2/shaders/preprocessor.html',
        bug=478572)
    self.Fail('deqp/data/gles2/shaders/scoping.html',
        bug=478572)
    self.Fail('conformance/extensions/ext-sRGB.html',
        bug=540900)
    self.Fail('conformance/textures/misc/cube-incomplete-fbo.html',
        bug=559362)

    # Win failures
    self.Fail('conformance/glsl/bugs/' +
              'pow-of-small-constant-in-user-defined-function.html',
        ['win'], bug=485641)
    self.Fail('conformance/glsl/bugs/sampler-struct-function-arg.html',
        ['win'], bug=485642)
    self.Fail('conformance/glsl/constructors/' +
              'glsl-construct-vec-mat-index.html',
              ['win'], bug=525188)

    # Win7 / Intel failures
    self.Fail('conformance/rendering/gl-scissor-test.html',
        ['win7', 'intel'], bug=314997)
    self.Fail('conformance/context/premultiplyalpha-test.html',
        ['win7', 'intel'])
    self.Fail('conformance/textures/misc/copy-tex-image-and-sub-image-2d.html',
        ['win7', 'intel'])
    self.Fail('conformance/rendering/gl-viewport-test.html',
        ['win7', 'intel'], bug=372511)

    # Win / AMD flakiness seen on new tryservers.
    # It's unfortunate that this suppression needs to be so broad, but
    # basically any test that uses readPixels is potentially flaky, and
    # it's infeasible to suppress individual failures one by one.
    self.Flaky('conformance/*', ['win', ('amd', 0x6779)], bug=491419)

    # Win / AMD D3D9 failures
    self.Fail('conformance/textures/misc/texparameter-test.html',
        ['win', 'amd', 'd3d9'], bug=839) # angle bug ID
    self.Fail('conformance/extensions/angle-instanced-arrays.html',
        ['win', 'amd', 'd3d9'], bug=475095)
    self.Fail('conformance/rendering/more-than-65536-indices.html',
        ['win', 'amd', 'd3d9'], bug=475095)

    # Win / D3D9 failures
    # Skipping these two tests because they're causing assertion failures.
    self.Skip('conformance/extensions/oes-texture-float-with-canvas.html',
        ['win', 'd3d9'], bug=896) # angle bug ID
    self.Skip('conformance/extensions/oes-texture-half-float-with-canvas.html',
        ['win', 'd3d9'], bug=896) # angle bug ID
    self.Fail('conformance/glsl/bugs/floor-div-cos-should-not-truncate.html',
        ['win', 'd3d9'], bug=1179) # angle bug ID

    # WIN / D3D9 / Intel failures
    self.Fail('conformance/ogles/GL/cos/cos_001_to_006.html',
        ['win', 'intel', 'd3d9'], bug=540538)

    # Win / OpenGL failures
    self.Fail('conformance/context/'+
        'context-attributes-alpha-depth-stencil-antialias.html',
        ['win', 'opengl'], bug=1007) # angle bug ID
    self.Fail('deqp/data/gles2/shaders/conditionals.html',
        ['win', 'opengl'], bug=1007) # angle bug ID

    # Win / OpenGL / NVIDIA failures
    self.Fail('conformance/attribs/gl-disabled-vertex-attrib.html',
        ['win', 'nvidia', 'opengl'], bug=1007) # angle bug ID

    # Win / OpenGL / AMD failures
    self.Skip('conformance/glsl/misc/shader-struct-scope.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID
    self.Skip('conformance/glsl/misc/shaders-with-invariance.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID
    self.Fail('conformance/glsl/misc/struct-nesting-of-variable-names.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID
    self.Fail('conformance/rendering/gl-scissor-test.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID
    self.Fail('deqp/data/gles2/shaders/constant_expressions.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID
    self.Fail('deqp/data/gles2/shaders/constants.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID
    self.Fail('deqp/data/gles2/shaders/swizzles.html',
        ['win', 'amd', 'opengl'], bug=1007) # angle bug ID

    # Win / OpenGL / Intel failures
    self.Fail('conformance/extensions/webgl-draw-buffers.html',
        ['win', 'intel', 'opengl'], bug=1007) # angle bug ID
    self.Fail('conformance/glsl/functions/glsl-function-normalize.html',
        ['win', 'intel', 'opengl'], bug=1007) # angle bug ID
    self.Fail('conformance/glsl/misc/shader-struct-scope.html',
        ['win', 'intel', 'opengl'], bug=1007) # angle bug ID
    self.Fail('conformance/uniforms/uniform-default-values.html',
        ['win', 'intel', 'opengl'], bug=1007) # angle bug ID

    # Mac failures
    self.Fail('conformance/glsl/misc/shaders-with-invariance.html',
        ['mac'], bug=421710)

    # Mac / Intel failures
    # Radar 13499466
    self.Fail('conformance/limits/gl-max-texture-dimensions.html',
        ['mac', 'intel'], bug=225642)
    # Radar 13499623
    self.Fail('conformance/textures/misc/texture-size.html',
        ['mac', 'intel'], bug=225642)

    # Mac / Intel HD 3000 failures
    self.Skip('conformance/ogles/GL/control_flow/control_flow_009_to_010.html',
        ['mac', ('intel', 0x116)], bug=322795)
    # Radar 13499677
    self.Fail('conformance/glsl/functions/' +
        'glsl-function-smoothstep-gentype.html',
        ['mac', ('intel', 0x116)], bug=225642)
    self.Fail('conformance/extensions/webgl-draw-buffers.html',
        ['mac', ('intel', 0x116)], bug=369349)

    # Mac 10.8 / Intel HD 3000 failures
    self.Fail('conformance/rendering/gl-scissor-test.html',
        ['mountainlion', ('intel', 0x116)], bug=314997)
    self.Fail('conformance/ogles/GL/operators/operators_009_to_016.html',
        ['mountainlion', ('intel', 0x116)], bug=322795)
    self.Flaky('conformance/ogles/*',
        ['mountainlion', ('intel', 0x116)], bug=527250)

    # Mac 10.8 / Intel HD 4000 failures.
    self.Fail('conformance/context/context-hidden-alpha.html',
        ['mountainlion', ('intel', 0x166)], bug=518008)

    # Mac 10.9 / Intel HD 3000 failures
    self.Fail('conformance/ogles/GL/operators/operators_009_to_016.html',
        ['mavericks', ('intel', 0x116)], bug=417415)
    self.Fail('conformance/rendering/gl-scissor-test.html',
        ['mavericks', ('intel', 0x116)], bug=417415)

    # Mac Retina failures
    self.Fail(
        'conformance/glsl/bugs/array-of-struct-with-int-first-position.html',
        ['mac', ('nvidia', 0xfd5), ('nvidia', 0xfe9)], bug=368912)

    # Mac / AMD Failures
    self.Fail('deqp/data/gles2/shaders/conversions.html',
        ['mac', 'amd'], bug=478572)

    # Mac 10.8 / ATI failures
    self.Fail(
        'conformance/rendering/' +
        'point-with-gl-pointcoord-in-fragment-shader.html',
        ['mountainlion', 'amd'])

    # Mac 10.7 / Intel failures
    self.Skip('conformance/glsl/functions/glsl-function-asin.html',
        ['lion', 'intel'])
    self.Skip('conformance/glsl/functions/glsl-function-dot.html',
        ['lion', 'intel'])
    self.Skip('conformance/glsl/functions/glsl-function-faceforward.html',
        ['lion', 'intel'])
    self.Skip('conformance/glsl/functions/glsl-function-length.html',
        ['lion', 'intel'])
    self.Skip('conformance/glsl/functions/glsl-function-normalize.html',
        ['lion', 'intel'])
    self.Skip('conformance/glsl/functions/glsl-function-reflect.html',
        ['lion', 'intel'])
    self.Skip('conformance/rendering/line-loop-tri-fan.html',
        ['lion', 'intel'])
    self.Skip('conformance/ogles/GL/control_flow/control_flow_001_to_008.html',
        ['lion', 'intel'], bug=345575)
    self.Skip('conformance/ogles/GL/dot/dot_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    self.Skip('conformance/ogles/GL/faceforward/faceforward_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    self.Skip('conformance/ogles/GL/length/length_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    self.Skip('conformance/ogles/GL/normalize/normalize_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    self.Skip('conformance/ogles/GL/reflect/reflect_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    self.Skip('conformance/ogles/GL/refract/refract_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    self.Skip('conformance/ogles/GL/tan/tan_001_to_006.html',
        ['lion', 'intel'], bug=323736)
    # Two flaky tests.
    self.Fail('conformance/ogles/GL/functions/functions_049_to_056.html',
        ['lion', 'intel'], bug=393331)
    self.Fail('conformance/extensions/webgl-compressed-texture-size-limit.html',
        ['lion', 'intel'], bug=393331)

    # Linux failures
    # NVIDIA
    self.Fail('conformance/textures/misc/default-texture.html',
        ['linux', ('nvidia', 0x104a)], bug=422152)
    self.Fail('conformance/extensions/angle-instanced-arrays.html',
              ['linux', 'nvidia'], bug=544989) # Too flaky to retry
    self.Flaky('conformance/extensions/oes-element-index-uint.html',
               ['linux', 'nvidia'], bug=524144)
    # AMD
    self.Flaky('conformance/more/functions/uniformi.html',
               ['linux', 'amd'], bug=550989)
    # AMD Radeon 5450
    self.Fail('conformance/programs/program-test.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/rendering/multisample-corruption.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/misc/default-texture.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/video/' +
        'tex-image-and-sub-image-2d-with-video-rgb-rgb-unsigned_byte.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgb-rgb-unsigned_short_5_6_5.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/video/' +
        'tex-image-and-sub-image-2d-with-video-rgba-rgba-unsigned_byte.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_short_4_4_4_4.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_short_5_5_5_1.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/webgl_canvas/tex-image-and-sub-image-2d-' +
        'with-webgl-canvas-rgb-rgb-unsigned_byte.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/webgl_canvas/tex-image-and-sub-image-2d-' +
        'with-webgl-canvas-rgb-rgb-unsigned_short_5_6_5.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/webgl_canvas/tex-image-and-sub-image-2d-' +
        'with-webgl-canvas-rgba-rgba-unsigned_byte.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/webgl_canvas/tex-image-and-sub-image-2d-' +
        'with-webgl-canvas-rgba-rgba-unsigned_short_4_4_4_4.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/webgl_canvas/tex-image-and-sub-image-2d-' +
        'with-webgl-canvas-rgba-rgba-unsigned_short_5_5_5_1.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/misc/texture-mips.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/misc/texture-npot-video.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/textures/misc/texture-size.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/more/functions/copyTexSubImage2D.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/more/functions/drawArraysOutOfBounds.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/more/functions/texImage2DHTML.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    self.Fail('conformance/more/functions/texSubImage2DHTML.html',
        ['linux', ('amd', 0x68f9)], bug=436212)
    # AMD Radeon 6450
    self.Fail('conformance/extensions/angle-instanced-arrays.html',
        ['linux', ('amd', 0x6779)], bug=479260)
    self.Fail('conformance/extensions/ext-texture-filter-anisotropic.html',
        ['linux', ('amd', 0x6779)], bug=436212)
    self.Fail('conformance/glsl/misc/shader-struct-scope.html',
        ['linux', ('amd', 0x6779)], bug=436212)
    self.Fail('conformance/glsl/misc/struct-nesting-of-variable-names.html',
        ['linux', ('amd', 0x6779)], bug=436212)
    self.Fail('conformance/rendering/point-size.html',
        ['linux', ('amd', 0x6779)], bug=436212)
    self.Fail('conformance/textures/misc/texture-sub-image-cube-maps.html',
        ['linux', ('amd', 0x6779)], bug=436212)
    self.Fail('conformance/more/functions/uniformf.html',
        ['linux', ('amd', 0x6779)], bug=436212)
    self.Fail('conformance/glsl/misc/shaders-with-invariance.html',
        ['linux', ('amd', 0x6779)], bug=479952)
    self.Fail('conformance/textures/misc/texture-mips.html',
        ['linux', ('amd', 0x6779)], bug=479981)
    self.Fail('conformance/textures/misc/texture-size-cube-maps.html',
        ['linux', ('amd', 0x6779)], bug=479983)
    self.Fail('conformance/uniforms/uniform-default-values.html',
        ['linux', ('amd', 0x6779)], bug=482013)

    # Android failures
    self.Fail('deqp/data/gles2/shaders/constants.html',
        ['android'], bug=478572)
    self.Fail('deqp/data/gles2/shaders/conversions.html',
        ['android'], bug=478572)
    self.Fail('deqp/data/gles2/shaders/declarations.html',
        ['android'], bug=478572)
    self.Fail('deqp/data/gles2/shaders/linkage.html',
        ['android'], bug=478572)
    # The following WebView crashes are causing problems with further
    # tests in the suite, so skip them for now.
    self.Skip('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgb-rgb-unsigned_byte.html',
        ['android', 'android-webview-shell'], bug=352645)
    self.Skip('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgb-rgb-unsigned_short_5_6_5.html',
        ['android', 'android-webview-shell'], bug=352645)
    self.Skip('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_byte.html',
        ['android', 'android-webview-shell'], bug=352645)
    self.Skip('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_short_4_4_4_4.html',
        ['android', 'android-webview-shell'], bug=352645)
    self.Skip('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_short_5_5_5_1.html',
        ['android', 'android-webview-shell'], bug=352645)
    self.Skip('conformance/textures/misc/texture-npot-video.html',
        ['android', 'android-webview-shell'], bug=352645)
    # Recent regressions have caused these to fail on multiple devices
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgb-rgb-unsigned_byte.html',
        ['android', 'android-content-shell'], bug=499555)
    self.Fail('conformance/textures/misc/texture-npot-video.html',
        ['android', 'android-content-shell'], bug=520638)
    # These are failing on the Nexus 5 and 6
    self.Fail('conformance/extensions/oes-texture-float-with-canvas.html',
              ['android', 'qualcomm'], bug=499555)
    # This crashes in Android WebView on the Nexus 6, preventing the
    # suite from running further. Rather than add multiple
    # suppressions, skip it until it's passing at least in content
    # shell.
    self.Skip('conformance/extensions/oes-texture-float-with-video.html',
              ['android', 'qualcomm'], bug=499555)
    # Nexus 5 failures
    self.Fail('conformance/glsl/bugs/struct-constructor-highp-bug.html',
              ['android', ('qualcomm', 'Adreno (TM) 330')], bug=559342)
    # Nexus 6 failures only
    self.Fail('conformance/context/' +
              'context-attributes-alpha-depth-stencil-antialias.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/context/premultiplyalpha-test.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/extensions/oes-texture-float-with-image-data.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/extensions/oes-texture-float-with-image.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_byte.html',
        ['android', 'android-content-shell',
         ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgb-rgb-unsigned_short_5_6_5.html',
        ['android', 'android-content-shell',
         ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_short_4_4_4_4.html',
        ['android', 'android-content-shell',
         ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/video/tex-image-and-sub-image-2d-with-' +
        'video-rgba-rgba-unsigned_short_5_5_5_1.html',
        ['android', 'android-content-shell',
         ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    # bindBufferBadArgs is causing the GPU thread to crash, taking
    # down the WebView shell, causing the next test to fail and
    # subsequent tests to be aborted.
    self.Skip('conformance/more/functions/bindBufferBadArgs.html',
              ['android', 'android-webview-shell',
               ('qualcomm', 'Adreno (TM) 420')], bug=499874)
    self.Fail('conformance/rendering/gl-scissor-test.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/misc/' +
              'copy-tex-image-and-sub-image-2d.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/misc/' +
              'tex-image-and-sub-image-2d-with-array-buffer-view.html',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/canvas/*',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/image_data/*',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/image/*',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    self.Fail('conformance/textures/webgl_canvas/*',
              ['android', ('qualcomm', 'Adreno (TM) 420')], bug=499555)
    # Nexus 9 failures
    self.Skip('conformance/extensions/oes-texture-float-with-video.html',
              ['android', 'nvidia'], bug=499555) # flaky

    # The following test is very slow and therefore times out on Android bot.
    self.Skip('conformance/rendering/multisample-corruption.html',
        ['android'])

    # ChromeOS: affecting all devices.
    self.Fail('conformance/extensions/webgl-depth-texture.html',
        ['chromeos'], bug=382651)

    # ChromeOS: all Intel except for pinetrail (stumpy, parrot, peppy,...)
    # We will just include pinetrail here for now as we don't want to list
    # every single Intel device ID.
    self.Fail('conformance/glsl/misc/empty_main.vert.html',
        ['chromeos', 'intel'], bug=375556)
    self.Fail('conformance/glsl/misc/gl_position_unset.vert.html',
        ['chromeos', 'intel'], bug=375556)
    self.Fail('conformance/glsl/misc/shaders-with-varyings.html',
        ['chromeos', 'intel'], bug=375556)
    self.Fail('conformance/renderbuffers/framebuffer-object-attachment.html',
        ['chromeos', 'intel'], bug=375556)
    self.Fail('conformance/textures/misc/texture-size-limit.html',
        ['chromeos', 'intel'], bug=385361)

    # ChromeOS: pinetrail (alex, mario, zgb).
    self.Fail('conformance/attribs/gl-vertex-attrib-render.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/glsl/functions/glsl-function-atan-xy.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/glsl/functions/glsl-function-cos.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/glsl/functions/glsl-function-sin.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/glsl/variables/gl-frontfacing.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/limits/gl-max-texture-dimensions.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/acos/acos_001_to_006.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/asin/asin_001_to_006.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/atan/atan_001_to_008.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/build/build_009_to_016.html',
        ['chromeos', ('intel', 0xa011)], bug=378938)
    self.Fail('conformance/ogles/GL/control_flow/control_flow_001_to_008.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/cos/cos_001_to_006.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/discard/discard_001_to_002.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_001_to_008.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_065_to_072.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_081_to_088.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_097_to_104.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_105_to_112.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_113_to_120.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/functions/functions_121_to_126.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail(
        'conformance/ogles/GL/gl_FrontFacing/gl_FrontFacing_001_to_001.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/log/log_001_to_008.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/log2/log2_001_to_008.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/normalize/normalize_001_to_006.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/ogles/GL/sin/sin_001_to_006.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/rendering/point-size.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/rendering/polygon-offset.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/textures/misc/texture-mips.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/textures/misc/texture-npot.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/textures/misc/texture-npot-video.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/textures/misc/texture-size.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Fail('conformance/uniforms/gl-uniform-arrays.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
    self.Skip('conformance/uniforms/uniform-default-values.html',
        ['chromeos', ('intel', 0xa011)], bug=375554)
