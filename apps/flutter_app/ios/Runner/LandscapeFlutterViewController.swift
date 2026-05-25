import Flutter
import UIKit

class LandscapeFlutterViewController: FlutterViewController {
  override func viewDidAppear(_ animated: Bool) {
    super.viewDidAppear(animated)
    requestLandscape()
  }

  override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
    return .landscape
  }

  override var preferredInterfaceOrientationForPresentation: UIInterfaceOrientation {
    return .landscapeRight
  }

  override var shouldAutorotate: Bool {
    return true
  }

  private func requestLandscape() {
    guard let windowScene = view.window?.windowScene else {
      UIViewController.attemptRotationToDeviceOrientation()
      return
    }
    if #available(iOS 16.0, *) {
      windowScene.requestGeometryUpdate(
        UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .landscape)
      ) { error in
        NSLog("AetherKiri view controller landscape request failed: \(error)")
      }
      setNeedsUpdateOfSupportedInterfaceOrientations()
    }
    UIViewController.attemptRotationToDeviceOrientation()
  }
}
