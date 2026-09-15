plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "eu.lielu.arcextract"
    compileSdk = 34

    defaultConfig {
        applicationId = "eu.lielu.arcextract"
        // minSdk 26 (Android 8.0): ACTION_OPEN_DOCUMENT_TREE has existed
        // since 21, but persistable URI permissions and scoped-storage
        // behavior are much more predictable from 26 on, and the target
        // device (a 2026 Galaxy Z Fold) is obviously far above this floor.
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            // Only the real target device's ABI — see docs/ANALYSIS.md.
            // Add "x86_64" here too if you need to run on the emulator.
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            // Points at the *repository-root* native/CMakeLists.txt (see
            // docs/ANALYSIS.md §6) rather than duplicating it inside
            // app/src/main/cpp, so the exact same CMake project also
            // builds and tests on a desktop host without Android tooling.
            path = file("../native/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // Versioned so every build (CI included) signs with the same debug key: a
    // CI runner has no ~/.android/debug.keystore of its own, so without this
    // AGP generates a random one per build, and two successive debug APKs
    // then can't install over each other (Android refuses the signature
    // mismatch, forcing an uninstall between every test build). Passwords are
    // deliberately public — they're Android's own standard debug keystore
    // defaults, not a secret; release builds don't use this config.
    signingConfigs {
        getByName("debug") {
            storeFile = file("debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
    sourceSets {
        getByName("main") {
            java.srcDirs("src/main/java")
        }
    }
}

dependencies {
    implementation(project(":core"))

    implementation(libs.core.ktx)
    implementation(libs.documentfile)
    implementation(libs.lifecycle.runtime.ktx)
    implementation(libs.lifecycle.viewmodel.compose)
    implementation(libs.lifecycle.viewmodel.ktx)
    implementation(libs.activity.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.graphics)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.compose.material3)
    implementation(libs.compose.material.icons.extended)
    implementation(libs.navigation.compose)
    implementation(libs.kotlinx.coroutines.android)

    testImplementation(libs.junit)
    testImplementation(libs.truth)
    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(platform(libs.compose.bom))

    debugImplementation(libs.compose.ui.tooling)
}
