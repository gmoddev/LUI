plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }
val FixtureDirectory = providers.gradleProperty("LuiFixtureDirectory").orNull
    ?: error("Supply -PLuiFixtureDirectory with the host-generated captured fixture directory")
android {
    namespace = "dev.lui.feasibility"
    compileSdk = 36
    buildToolsVersion = "36.0.0"
    ndkVersion = "28.2.13676358"
    useLibrary("android.test.runner")
    useLibrary("android.test.base")
    defaultConfig {
        applicationId = "dev.lui.feasibility"
        minSdk = 30
        targetSdk = 36
        versionCode = 1
        versionName = "0.0.1-8E"
        testInstrumentationRunner = "android.test.InstrumentationTestRunner"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake {
            cppFlags += listOf("-O2", "-DNDEBUG") // Optimized native release profile; APK remains debuggable.
            arguments += listOf("-DLUI_BUILD_MOBILE_FEASIBILITY=ON", "-DANDROID_STL=c++_static")
            providers.gradleProperty("LuiLuauSource").orNull?.let { arguments += "-DLUAU_SOURCE_DIR=$it" }
            providers.gradleProperty("LuiJsonSource").orNull?.let { arguments += "-DFETCHCONTENT_SOURCE_DIR_LUIJSON=$it" }
            targets += "LuiMobileJni"
        } }
    }
    externalNativeBuild { cmake { path = file("../../../CMakeLists.txt"); version = "3.31.6" } }
    sourceSets.getByName("main").assets.srcDir(FixtureDirectory)
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    packaging { jniLibs.useLegacyPackaging = false }
}
