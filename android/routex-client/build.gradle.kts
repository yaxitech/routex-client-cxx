import com.vanniktech.maven.publish.AndroidSingleVariantLibrary
import org.gradle.plugins.signing.Sign
import java.io.ByteArrayOutputStream
import javax.inject.Inject

plugins {
    id("com.android.library")
    id("com.vanniktech.maven.publish") version "0.37.0"
}

group = "tech.yaxi"

// The crate manifest is the one place the version lives - it names the version in
// development, bumped right after each release, and the static libraries take
// theirs from the same file. `-Prelease` publishes exactly that; anything else
// publishes a snapshot of it, so a release can never carry a number the manifest
// does not have.
val crateVersion =
    Regex("""(?sm)\[package].*?^version\s*=\s*"([^"]+)"\s*$""")
        .find(file("../../Cargo.toml").readText())
        ?.groupValues
        ?.get(1)
        ?: error("no [package] version in the crate manifest at ../../Cargo.toml")

version = if (providers.gradleProperty("release").isPresent) crateVersion else "$crateVersion-SNAPSHOT"

// The Rust static libraries are cross-compiled outside Gradle, one per ABI, and
// pointed at with `-Pyaxi.libDir=<dir>` (or YAXI_LIB_DIR), where <dir> holds one
// subdirectory per ABI. CMake reports a usable error when this is unset, so
// leave configuration alone.
val yaxiLibDir =
    providers.gradleProperty("yaxi.libDir")
        .orElse(providers.environmentVariable("YAXI_LIB_DIR"))
        .getOrElse("")

android {
    namespace = "tech.yaxi.routex.cxx"
    compileSdk = 36

    // When the environment provides an Android NDK outside an SDK install,
    // point AGP at it directly.
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
        // API 26 is what the Rust static libraries link against.
        // Every abi.json in the published package repeats this number, so the
        // two have to be changed together.
        minSdk = 26

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DYAXI_LIB_DIR=$yaxiLibDir",
                    "-DANDROID_STL=c++_shared",
                )
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
        prefabPublishing = true
    }

    // Two modules over the same headers: consumers link one or the other, never
    // both — the static module carries the same symbols as the shared one.
    prefab {
        create("routex-client") {
            headers = "../../include"
        }
        create("routex-client-static") {
            headers = "../../include"
        }
    }

    packaging {
        // The shared library reaches the consumer's APK through the prefab
        // package; the jni/ copy AGP adds by default would ship it twice.
        jniLibs.excludes += "**/libroutex-client.so"
    }
}

mavenPublishing {
    // Maven Central rejects releases without sources and javadoc jars, even for
    // a package whose only content is native code, so both are generated empty.
    configure(AndroidSingleVariantLibrary(variant = "release"))

    coordinates("tech.yaxi", "routex-client-cxx", version.toString())

    publishToMavenCentral()

    // Releases must be GPG-signed; snapshots and local publishing work without
    // a key so the CI snapshot job and ad-hoc `publishToMavenLocal` both run.
    val isSnapshot = version.toString().endsWith("-SNAPSHOT")
    val isLocalPublish =
        gradle.startParameter.taskNames.any { it.endsWith("publishToMavenLocal", ignoreCase = true) }
    if (!isSnapshot && !isLocalPublish) {
        signAllPublications()
    }

    pom {
        name.set("routex-client-cxx")
        description.set("C++ client for YAXI's Open Banking services, as an Android prefab package")
        url.set("https://github.com/yaxitech/routex-client-cxx/")
        inceptionYear.set("2026")
        licenses {
            license {
                name.set("The Apache Software License, Version 2.0")
                url.set("https://www.apache.org/licenses/LICENSE-2.0.txt")
                distribution.set("repo")
            }
        }
        developers {
            developer {
                id.set("yaxitech")
                name.set("YAXI")
                email.set("hello@yaxi.tech")
                organization.set("YAXI")
                organizationUrl.set("https://yaxi.tech")
            }
        }
        scm {
            url.set("https://github.com/yaxitech/routex-client-cxx/")
            connection.set("scm:git:git://github.com/yaxitech/routex-client-cxx.git")
            developerConnection.set("scm:git:ssh://git@github.com/yaxitech/routex-client-cxx.git")
        }
    }
}

// The AAR is signed with jarsigner before anything publishes or checksums it.
// The keystore and alias come from properties or the environment; the password
// only ever from the environment, so it stays out of the command line and out of
// Gradle's input hashes.
abstract class SignAar : DefaultTask() {
    @get:InputFile
    abstract val aar: RegularFileProperty

    @get:InputFile
    abstract val keystore: RegularFileProperty

    @get:Input
    abstract val alias: Property<String>

    @get:Input
    abstract val passwordEnvVar: Property<String>

    @get:Input
    @get:Optional
    abstract val timestampAuthority: Property<String>

    @get:Inject
    abstract val execOperations: ExecOperations

    @TaskAction
    fun sign() {
        val jarsigner = File(System.getProperty("java.home"), "bin/jarsigner")
        val arguments =
            buildList {
                add(jarsigner.absolutePath)
                addAll(listOf("-keystore", keystore.get().asFile.absolutePath))
                addAll(listOf("-storepass:env", passwordEnvVar.get()))
                addAll(listOf("-digestalg", "SHA-256"))
                addAll(listOf("-sigalg", "SHA256withRSA"))
                // Without a timestamp the signature stops verifying the day the
                // certificate expires, and a published artifact outlives it.
                timestampAuthority.orNull?.let { addAll(listOf("-tsa", it)) }
                add(aar.get().asFile.absolutePath)
                add(alias.get())
            }
        execOperations.exec { commandLine(arguments) }

        // jarsigner exits 0 on an unsigned archive, so read its (localized) verdict.
        val verification = ByteArrayOutputStream()
        execOperations.exec {
            commandLine(
                jarsigner.absolutePath,
                "-J-Duser.language=en",
                "-verify",
                aar.get().asFile.absolutePath,
            )
            standardOutput = verification
        }
        if (!verification.toString().contains("jar verified.")) {
            throw GradleException("The signed AAR does not verify:\n$verification")
        }
    }
}

val keystorePath =
    providers.gradleProperty("yaxi.signing.keystore")
        .orElse(providers.environmentVariable("YAXI_SIGNING_KEYSTORE"))
val keystoreAlias =
    providers.gradleProperty("yaxi.signing.alias")
        .orElse(providers.environmentVariable("YAXI_SIGNING_ALIAS"))

if (keystorePath.isPresent) {
    val signReleaseAar =
        tasks.register<SignAar>("signReleaseAar") {
            aar.set(layout.buildDirectory.file("outputs/aar/${project.name}-release.aar"))
            keystore.set(file(keystorePath.get()))
            alias.set(keystoreAlias.orElse("yaxi"))
            passwordEnvVar.set("YAXI_SIGNING_STOREPASS")
            timestampAuthority.set(
                providers.gradleProperty("yaxi.signing.tsa")
                    .orElse(providers.environmentVariable("YAXI_SIGNING_TSA")),
            )
            // The task rewrites its own input, so it can never be up to date.
            outputs.upToDateWhen { false }
            dependsOn("bundleReleaseAar")
        }

    // Everything that reads the AAR has to see the signed bytes: the GPG
    // signatures and checksums are computed over the file as published.
    tasks.withType<Sign>().configureEach { dependsOn(signReleaseAar) }
    tasks.withType<PublishToMavenRepository>().configureEach { dependsOn(signReleaseAar) }
    tasks.withType<PublishToMavenLocal>().configureEach { dependsOn(signReleaseAar) }
} else if (providers.gradleProperty("release").isPresent) {
    throw GradleException(
        "A release must be signed: pass -Pyaxi.signing.keystore=<path> (alias via " +
            "-Pyaxi.signing.alias, default \"yaxi\") with the store password in " +
            "YAXI_SIGNING_STOREPASS.",
    )
}
