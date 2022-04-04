#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

using namespace std;
using namespace chrono;

using counter_t = uint32_t;

thread worker_thread_maker(
        const shared_ptr<mutex> &mtx,
        const shared_ptr<atomic<counter_t>> &queue,
        const shared_ptr<atomic<counter_t>> &other_queue,
        const shared_ptr<atomic_uint8_t> &total_on_bridge,
        counter_t difference,
        const bool positive_difference
) {
    using random_duration_type = unsigned short;

    constexpr uint8_t THRESHOLD = 5;

    return thread([mtx, queue, other_queue, total_on_bridge, difference, positive_difference]() mutable -> void {
        random_device randomDevice;
        default_random_engine randomEngine(randomDevice());
        uniform_int_distribution<random_duration_type> intDistribution(100, 500);

        shared_ptr<atomic<uint8_t>> self_on_bridge(make_shared<atomic<uint8_t>>(uint8_t(0)));

        for (;;) {
            counter_t queue_loaded = queue->load(memory_order::acquire);

            if (!queue_loaded) {
                break;
            }

            if (
                    other_queue->load(memory_order::acquire) + (positive_difference ? difference : 0) <
                    queue_loaded + (!positive_difference
                                    ? difference
                                    : 0) + THRESHOLD &&
                    self_on_bridge->load(memory_order::acquire) < 20
                    ) {
                lock_guard<mutex> guard(*mtx);

                if (total_on_bridge->load(memory_order::acquire) < 30) {
                    difference -= 1;

                    queue->fetch_sub(1, memory_order::acq_rel);

                    self_on_bridge->fetch_add(1, memory_order::acq_rel);

                    total_on_bridge->fetch_add(1, memory_order::acq_rel);

                    unsigned int sleep_time = intDistribution(randomEngine);

                    thread(
                            [sleep_time, self_on_bridge, total_on_bridge]() -> void {
                                this_thread::sleep_for(milliseconds(15 * sleep_time));

                                self_on_bridge->fetch_sub(1, memory_order::acq_rel);

                                total_on_bridge->fetch_sub(1, memory_order::acq_rel);
                            }
                    ).detach();
                }
            }

            this_thread::sleep_for(milliseconds(intDistribution(randomEngine)));
        }
    });
}

int main() {
    ifstream file("dunav.bin", ios_base::in | ios_base::binary);

    if (!file.is_open()) {
        cerr << "[ERROR] Couldn't open configuration file!" << endl;

        return 1;
    }

    counter_t inward_count, outward_count, on_bridge_count;

    file.read((char *) (&inward_count), sizeof inward_count);

    if (!file) {
        cerr << "[ERROR] Couldn't read inward count!" << endl;

        return 1;
    }

    file.read((char *) (&outward_count), sizeof outward_count);

    if (!file) {
        cerr << "[ERROR] Couldn't read outward count!" << endl;

        return 1;
    }

    file.close();

    const bool positive_difference = inward_count < outward_count;

    const counter_t difference = positive_difference ? outward_count - inward_count : inward_count - outward_count;

    cout << "Welcome to Dunav bridge!" << endl << endl;

    shared_ptr<mutex> mtx(make_shared<mutex>());

    shared_ptr<atomic<counter_t>> inward(
            make_shared<atomic<counter_t>>(
                    inward_count
            )
    ), outward(
            make_shared<atomic<counter_t>>(
                    outward_count
            )
    );

    shared_ptr<atomic<uint8_t>> on_bridge(make_shared<atomic<uint8_t>>(uint8_t(0)));

    thread inward_thread(
            worker_thread_maker(
                    mtx,
                    inward,
                    outward,
                    on_bridge,
                    difference,
                    !positive_difference
            )
    );

    thread outward_thread(
            worker_thread_maker(
                    mtx,
                    outward,
                    inward,
                    on_bridge,
                    difference,
                    positive_difference
            )
    );

    do {
        {
            lock_guard<mutex> guard(*mtx);

            inward_count = inward->load(memory_order::acquire);

            outward_count = outward->load(memory_order::acquire);

            on_bridge_count = on_bridge->load(memory_order::acquire);
        }

        cout << "Inward left: " << (unsigned int) (inward_count) << endl << "Outward left: "
             << (unsigned int) (outward_count) << endl
             << "Currently on bridge: " << (unsigned int) (on_bridge_count) << endl << endl;

        this_thread::sleep_for(seconds(1));
    } while (inward_count || outward_count || on_bridge_count);

    try {
        inward_thread.join();
    } catch (system_error &error) {
        cerr << "[ERROR:CODE=" << error.code() << "] " << error.what() << endl;
    }

    try {
        outward_thread.join();
    } catch (system_error &error) {
        cerr << "[ERROR:CODE=" << error.code() << "] " << error.what() << endl;
    }

    return 0;
}
