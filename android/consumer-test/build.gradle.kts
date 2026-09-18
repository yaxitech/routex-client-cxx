plugins {
    id("com.android.application")
}

// Consumes the prefab package exactly as a third-party NDK app does: headers via
// find_package, symbols via the imported target. Building it is the check — the
// APK it produces is what tells us the shared library reaches a consumer.
android {
    namespace = "tech.yaxi.routex.cxx.consumertest"
    compileSdk = 36

    System.getenv("ANDROID_NDK_ROOT")?.takeIf { it.isNotBlank() }?.let { root ->
        ndkPath = root
        // AGP compares ndkPath against its own default ndkVersion and fails the
        // build when they disagree, so take both from the same NDK.
        ndkVersion =
            file("$root/source.properties")
                .readLines()
                .first { it.startsWith("Pkg.Revision") }
                .substringAfter('=')
                .trim()
    }

    defaultConfig {
        applicationId = "tech.yaxi.routex.cxx.consumertest"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                // AGP defaults consumers to c++_static. The package requires
                // c++_shared, and so does every consumer of it — the API hands
                // std::string, std::vector and std::variant across the library
                // boundary, which only works with one shared STL in the process.
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    buildFeatures {
        prefab = true
    }
}

dependencies {
    implementation(project(":routex-client"))

    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("junit:junit:4.13.2")
}
