plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val hostNativeDir = layout.buildDirectory.dir("host-native")
val configureHostNative by tasks.registering(Exec::class) {
    environment("JAVA_HOME", System.getProperty("java.home"))
    commandLine("cmake", "-S", "src/main/cpp", "-B", hostNativeDir.get().asFile,
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug")
}
val buildHostNative by tasks.registering(Exec::class) {
    dependsOn(configureHostNative)
    commandLine("cmake", "--build", hostNativeDir.get().asFile,
        "--target", "tatra_jni", "--parallel", "2")
}

android {
    namespace = "pl.tatrapeak"
    compileSdk = 35
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "pl.tatrapeak"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                targets += "tatra_jni"
            }
        }
    }
    buildTypes {
        getByName("debug") { ndk { abiFilters += "x86_64" } }
    }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.31.6" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    sourceSets {
        getByName("test").java.srcDir("src/sharedTest/java")
        getByName("androidTest").java.srcDir("src/sharedTest/java")
    }
    testOptions.unitTests.all {
        it.dependsOn(buildHostNative)
        it.systemProperty("java.library.path", hostNativeDir.get().asFile.absolutePath)
        it.jvmArgs("-Xcheck:jni")
    }
    androidResources { noCompress += "tpk" }
}

dependencies {
    implementation("androidx.camera:camera-camera2:1.4.2")
    implementation("androidx.camera:camera-lifecycle:1.4.2")
    implementation("androidx.camera:camera-view:1.4.2")
    implementation("androidx.activity:activity-ktx:1.10.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:core-ktx:1.6.1")
}
