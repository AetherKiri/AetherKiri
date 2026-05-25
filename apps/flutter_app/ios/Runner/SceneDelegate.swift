import Flutter
import UIKit

class SceneDelegate: FlutterSceneDelegate {
  override func scene(
    _ scene: UIScene,
    willConnectTo session: UISceneSession,
    options connectionOptions: UIScene.ConnectionOptions
  ) {
    super.scene(scene, willConnectTo: session, options: connectionOptions)
    requestLandscape(scene)
  }

  override func sceneDidBecomeActive(_ scene: UIScene) {
    super.sceneDidBecomeActive(scene)
    requestLandscape(scene)
  }

  private func requestLandscape(_ scene: UIScene) {
    guard let windowScene = scene as? UIWindowScene else {
      return
    }
    if #available(iOS 16.0, *) {
      windowScene.requestGeometryUpdate(
        UIWindowScene.GeometryPreferences.iOS(interfaceOrientations: .landscape)
      ) { error in
        NSLog("AetherKiri landscape geometry request failed: \(error)")
      }
      for window in windowScene.windows {
        window.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
      }
    }
    UIViewController.attemptRotationToDeviceOrientation()
  }
}
