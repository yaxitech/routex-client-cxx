pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }

    plugins {
        id("com.android.application") version "9.3.2"
        id("com.android.library") version "9.3.2"
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "routex-client-cxx"

include(":routex-client")

include(":consumer-test")
