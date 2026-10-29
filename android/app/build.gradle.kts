plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
// Special thanks to the NTXMESH community, Meshcore Community, and Pat Steinhauer-Scates for their guidance and support.

android {
    namespace = "com.wolfreactive.cairn"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.wolfreactive.cairn.station"
        minSdk = 26
        targetSdk = 35
        versionCode = 120
        versionName = "1.2.0"
    }

    signingConfigs {
        // A shared ALPHA key so that each new build installs over the last one. It is committed to the repo on
        // purpose and is NOT a secret: anyone can sign an app with it. Replace it before the app is sold or
        // handed to anyone who needs to trust where it came from (see android/README.md).
        create("alpha") {
            storeFile = file("../alpha.keystore")
            storePassword = "cairn-alpha"
            keyAlias = "cairn-alpha"
            keyPassword = "cairn-alpha"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("alpha")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    lint { abortOnError = false }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.webkit:webkit:1.11.0")
    implementation("com.github.mik3y:usb-serial-for-android:3.8.1")
    testImplementation("junit:junit:4.13.2")
}
