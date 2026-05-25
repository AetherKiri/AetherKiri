#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_engine_bridge.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_engine_bridge'
  s.version          = '0.0.1'
  s.summary          = 'KrKr2 engine bridge for Flutter iOS.'
  s.description      = <<-DESC
    Bridges the KrKr2 C++ engine to Flutter iOS via FFI and texture.
  DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'Flutter'
  s.platform         = :ios, '15.0'
  s.static_framework = true

  # Pre-built engine static libraries (split to avoid duplicate symbols)
  # libengine_project.a = project code (force-loaded)
  # libengine_vendors.a = third-party vcpkg libs (normal linking)
  s.vendored_libraries = 'Libs/libengine_project.a', 'Libs/libengine_vendors.a'

  # System framework dependencies (required by SDL2, FFmpeg, OpenAL, ANGLE, etc.)
  s.frameworks = 'IOSurface', 'CoreVideo', 'Metal', 'QuartzCore', 'Accelerate',
                 'AudioToolbox', 'AVFoundation', 'CoreAudio', 'CoreFoundation',
                 'CoreGraphics', 'CoreMotion', 'CoreMedia', 'CoreBluetooth',
                 'CoreText', 'Foundation', 'UIKit', 'OpenGLES', 'VideoToolbox', 'Security'

  s.weak_frameworks = 'GameController', 'CoreHaptics'

  # System C/C++ libraries
  s.libraries = 'xml2', 'c++', 'z', 'bz2', 'iconv', 'resolv'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'IPHONEOS_DEPLOYMENT_TARGET' => '15.0',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    # -ObjC: load all .o files containing ObjC classes/categories
    # -force_load: force-load project library to ensure all C++ static initializers run
    # Do NOT use -all_load (causes duplicate symbols from vcpkg libs with overlapping code)
    'OTHER_LDFLAGS' => '-ObjC -force_load "$(PODS_TARGET_SRCROOT)/Libs/libengine_project.a"',
  }
  s.user_target_xcconfig = {
    # The Dart side uses DynamicLibrary.process() on iOS. Force-loading the
    # project archive into Runner keeps exported engine_* FFI symbols visible in
    # the final app executable even when no ObjC/Swift code references them.
    'OTHER_LDFLAGS' => '$(inherited) -Wl,-export_dynamic -force_load "$(PODS_ROOT)/../.symlinks/plugins/flutter_engine_bridge/ios/Libs/libengine_project.a"',
  }
  s.swift_version = '5.0'

  # If your plugin requires a privacy manifest, for example if it uses any
  # required reason APIs, update the PrivacyInfo.xcprivacy file to describe your
  # plugin's privacy impact, and then uncomment this line. For more information,
  # see https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
  # s.resource_bundles = {'flutter_engine_bridge_privacy' => ['Resources/PrivacyInfo.xcprivacy']}
end
