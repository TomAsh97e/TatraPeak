#include <jni.h>
#include <tatra/mobile.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct PendingJavaException {};
void checked(JNIEnv* env) {
    if (env->ExceptionCheck()) throw PendingJavaException{};
}
void javaError(JNIEnv* env, const char* type, const char* message) noexcept {
    if (env->ExceptionCheck()) return;
    const auto cls = env->FindClass(type);
    if (cls != nullptr) { env->ThrowNew(cls, message); env->DeleteLocalRef(cls); }
}
// Every exported entry point goes through this barrier, including create/destroy.
template <typename T, typename F> T boundary(JNIEnv* env, T fallback, F action) noexcept {
    try { return action(); }
    catch (const PendingJavaException&) { /* Preserve the pending JVM exception. */ }
    catch (const std::bad_alloc&) { javaError(env, "java/lang/OutOfMemoryError", "Native allocation failed"); }
    catch (const std::invalid_argument& e) { javaError(env, "java/lang/IllegalArgumentException", e.what()); }
    catch (const std::exception& e) { javaError(env, "java/lang/IllegalStateException", e.what()); }
    catch (...) { javaError(env, "java/lang/IllegalStateException", "Unknown native exception"); }
    return fallback;
}

void statusCheck(JNIEnv* env, tatra_mobile_status status, const char* message) {
    if (status == TATRA_MOBILE_OK) return;
    const auto cls = env->FindClass("pl/tatrapeak/engine/NativeEngineException");
    checked(env);
    const auto ctor = env->GetMethodID(cls, "<init>", "(ILjava/lang/String;)V");
    checked(env);
    // Core messages can contain dataset IDs. Decode real UTF-8, including supplementary
    // Unicode characters, rather than passing it to NewStringUTF (modified UTF-8).
    const auto stringClass = env->FindClass("java/lang/String");
    checked(env);
    const auto stringCtor = env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
    checked(env);
    const auto size = static_cast<jsize>(std::strlen(message));
    const auto bytes = env->NewByteArray(size);
    checked(env);
    if (size) env->SetByteArrayRegion(bytes, 0, size, reinterpret_cast<const jbyte*>(message));
    checked(env);
    const auto encoding = env->NewStringUTF("UTF-8");
    checked(env);
    const auto text = env->NewObject(stringClass, stringCtor, bytes, encoding);
    checked(env);
    const auto error = env->NewObject(cls, ctor, static_cast<jint>(status), text);
    checked(env);
    env->Throw(static_cast<jthrowable>(error));
    env->DeleteLocalRef(error);
    env->DeleteLocalRef(text);
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(stringClass);
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(encoding);
    throw PendingJavaException{};
}

jsize length(JNIEnv* env, jarray array) {
    if (array == nullptr) throw std::invalid_argument("Null JNI array");
    return env->GetArrayLength(array);
}
void requireLength(JNIEnv* env, jarray array, jsize expected) {
    if (length(env, array) != expected) throw std::invalid_argument("Incorrect JNI array length");
}
std::vector<double> doubles(JNIEnv* env, jdoubleArray array) {
    std::vector<double> result(static_cast<std::size_t>(length(env, array)));
    if (!result.empty()) env->GetDoubleArrayRegion(array, 0, static_cast<jsize>(result.size()), result.data());
    checked(env);
    return result;
}
std::string utf8(JNIEnv* env, jobjectArray array, jsize index) {
    const auto bytes = static_cast<jbyteArray>(env->GetObjectArrayElement(array, index));
    checked(env);
    const auto size = length(env, bytes);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size) env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(result.data()));
    env->DeleteLocalRef(bytes);
    checked(env);
    if (result.find('\0') != std::string::npos) throw std::invalid_argument("NUL in peak text");
    return result;
}
void putUtf8(JNIEnv* env, jobjectArray array, jsize index, const char* text) {
    const auto size = std::strlen(text);
    if (size > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
        throw std::invalid_argument("Peak text exceeds JVM array limit");
    const auto bytes = env->NewByteArray(static_cast<jsize>(size));
    checked(env);
    if (size) env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(text));
    checked(env);
    env->SetObjectArrayElement(array, index, bytes);
    env->DeleteLocalRef(bytes);
    checked(env);
}

struct EngineDeleter {
    void operator()(tatra_mobile_engine* p) const noexcept { tatra_mobile_engine_destroy(p); }
};
struct EngineState {
    std::unique_ptr<tatra_mobile_engine, EngineDeleter> engine;
    // Capacity reused across evaluates; C ABI strings are copied before unlocking.
    std::vector<tatra_mobile_observation> observations;
};
// Opaque monotonically increasing IDs reject stale handles instead of dereferencing freed pointers.
std::mutex registryMutex;
std::unordered_map<jlong, EngineState> engines;
jlong nextId = 1;

jobject frame(JNIEnv* env, const std::vector<tatra_mobile_observation>& observations,
              std::size_t count, const tatra_mobile_diagnostics& diagnostics) {
    if (count > static_cast<std::size_t>((std::numeric_limits<jsize>::max() - 4) / 9))
        throw std::invalid_argument("Result exceeds JVM array limit");
    const auto bytesClass = env->FindClass("[B");
    checked(env);
    const auto text = env->NewObjectArray(static_cast<jsize>(2 * count), bytesClass, nullptr);
    checked(env);
    std::vector<jdouble> values(count * 9);
    std::vector<jlong> counters(4 + count * 3);
    counters[0] = static_cast<jlong>(diagnostics.catalog_count);
    counters[1] = static_cast<jlong>(diagnostics.outside_range_count);
    counters[2] = static_cast<jlong>(diagnostics.projected_count);
    counters[3] = static_cast<jlong>(diagnostics.visibility_sample_count);
    constexpr auto absent = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& o = observations[i];
        putUtf8(env, text, static_cast<jsize>(2 * i), o.peak_id);
        putUtf8(env, text, static_cast<jsize>(2 * i + 1), o.peak_name);
        const auto d = 9 * i;
        values[d] = o.has_display_elevation ? o.display_elevation_m : absent;
        values[d + 1] = o.azimuth_deg;
        values[d + 2] = o.elevation_deg;
        values[d + 3] = o.slant_range_m;
        values[d + 4] = o.has_pixel ? o.pixel_u : absent;
        values[d + 5] = o.has_pixel ? o.pixel_v : absent;
        values[d + 6] = o.camera_depth_m;
        values[d + 7] = o.has_min_sampled_clearance ? o.min_sampled_clearance_m : absent;
        values[d + 8] = o.has_first_blocker_distance ? o.first_blocker_distance_m : absent;
        counters[4 + 3 * i] = o.projection_state;
        counters[5 + 3 * i] = o.visibility_state;
        counters[6 + 3 * i] = static_cast<jlong>(o.visibility_sample_count);
    }
    const auto ds = env->NewDoubleArray(static_cast<jsize>(values.size()));
    checked(env);
    if (!values.empty()) env->SetDoubleArrayRegion(ds, 0, static_cast<jsize>(values.size()), values.data());
    checked(env);
    const auto cs = env->NewLongArray(static_cast<jsize>(counters.size()));
    checked(env);
    env->SetLongArrayRegion(cs, 0, static_cast<jsize>(counters.size()), counters.data());
    checked(env);
    const auto cls = env->FindClass("pl/tatrapeak/engine/JniFrame");
    checked(env);
    const auto ctor = env->GetMethodID(cls, "<init>", "([[B[D[J)V");
    checked(env);
    const auto result = env->NewObject(cls, ctor, text, ds, cs);
    checked(env);
    return result; // Remaining local references are released when returning from JNI.
}
} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_pl_tatrapeak_engine_NativeBindings_abiVersion(JNIEnv* env, jobject) {
    return boundary<jint>(env, 0, [] { return static_cast<jint>(tatra_mobile_abi_version()); });
}
extern "C" JNIEXPORT jstring JNICALL
Java_pl_tatrapeak_engine_NativeBindings_version(JNIEnv* env, jobject) {
    return boundary<jstring>(env, nullptr, [&] { return env->NewStringUTF(tatra_mobile_version()); });
}
extern "C" JNIEXPORT jlong JNICALL
Java_pl_tatrapeak_engine_NativeBindings_create(JNIEnv* env, jobject, jint rows, jint columns,
    jdoubleArray geometry, jfloatArray heights, jbyteArray mask, jobjectArray text, jdoubleArray peaks) {
    return boundary<jlong>(env, 0, [&]() -> jlong {
        if (rows < 2 || columns < 2 || static_cast<jlong>(rows) * columns > std::numeric_limits<jsize>::max())
            throw std::invalid_argument("Invalid terrain dimensions or overflow");
        const auto samples = rows * columns;
        requireLength(env, geometry, 4);
        requireLength(env, heights, samples);
        requireLength(env, mask, samples);
        const auto textCount = length(env, text);
        if (textCount % 2 != 0 || textCount > std::numeric_limits<jsize>::max() / 2)
            throw std::invalid_argument("Invalid peak array lengths");
        requireLength(env, peaks, textCount * 2);
        const auto g = doubles(env, geometry);
        const auto p = doubles(env, peaks);
        std::vector<float> h(static_cast<std::size_t>(samples));
        std::vector<std::uint8_t> m(static_cast<std::size_t>(samples));
        env->GetFloatArrayRegion(heights, 0, samples, h.data()); checked(env);
        env->GetByteArrayRegion(mask, 0, samples, reinterpret_cast<jbyte*>(m.data())); checked(env);
        const auto count = static_cast<std::size_t>(textCount / 2);
        std::vector<std::string> strings(static_cast<std::size_t>(textCount));
        for (jsize i = 0; i < textCount; ++i) strings[static_cast<std::size_t>(i)] = utf8(env, text, i);
        std::vector<tatra_mobile_peak> records(count);
        for (std::size_t i = 0; i < count; ++i) {
            records[i] = {strings[2 * i].c_str(), strings[2 * i + 1].c_str(),
                p[4 * i], p[4 * i + 1], p[4 * i + 2],
                static_cast<std::uint8_t>(!std::isnan(p[4 * i + 3])), p[4 * i + 3]};
        }
        const tatra_mobile_create_info info{{static_cast<std::size_t>(rows), static_cast<std::size_t>(columns),
            g[0], g[1], g[2], g[3], h.data(), m.data(), h.size()}, records.data(), count};
        tatra_mobile_engine* raw = nullptr;
        char error[512]{};
        const auto status = tatra_mobile_engine_create(&info, &raw, error, sizeof(error));
        EngineState state{std::unique_ptr<tatra_mobile_engine, EngineDeleter>{raw}, {}};
        statusCheck(env, status, error);
        state.observations.resize(tatra_mobile_engine_peak_count(raw));
        std::lock_guard lock(registryMutex);
        if (nextId == std::numeric_limits<jlong>::max()) throw std::runtime_error("Native handle IDs exhausted");
        const auto id = nextId++;
        engines.emplace(id, std::move(state));
        return id;
    });
}
extern "C" JNIEXPORT jobject JNICALL
Java_pl_tatrapeak_engine_NativeBindings_evaluate(JNIEnv* env, jobject, jlong handle, jlong timestamp,
    jboolean simulated, jint width, jint height, jlong maxSamples, jdoubleArray values) {
    return boundary<jobject>(env, nullptr, [&] {
        requireLength(env, values, 18);
        if (maxSamples < 0 || static_cast<std::uint64_t>(maxSamples) > std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("Invalid visibility sample limit");
        const auto q = doubles(env, values);
        const tatra_mobile_query query{timestamp, q[0], q[1], q[2], static_cast<std::uint8_t>(simulated),
            q[3], q[4], q[5], q[6], width, height, q[7], q[8], q[9], q[10],
            q[11], q[12], q[13], q[14], q[15], q[16], q[17], static_cast<std::size_t>(maxSamples)};
        std::lock_guard lock(registryMutex);
        const auto entry = engines.find(handle);
        if (entry == engines.end()) throw std::runtime_error("Invalid or closed native handle");
        auto& state = entry->second;
        std::size_t count = 0;
        tatra_mobile_diagnostics diagnostics{};
        char error[512]{};
        statusCheck(env, tatra_mobile_engine_evaluate(state.engine.get(), &query, state.observations.data(),
            state.observations.size(), &count, &diagnostics, error, sizeof(error)), error);
        return frame(env, state.observations, count, diagnostics);
    });
}
extern "C" JNIEXPORT void JNICALL
Java_pl_tatrapeak_engine_NativeBindings_destroy(JNIEnv* env, jobject, jlong handle) {
    boundary<int>(env, 0, [&] {
        std::lock_guard lock(registryMutex);
        engines.erase(handle); // destroy(0) and repeated destruction are safe.
        return 0;
    });
}
