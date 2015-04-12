#include "SDRThread.h"
#include "CubicSDRDefs.h"
#include <vector>
#include "CubicSDR.h"

SDRThread::SDRThread(SDRThreadCommandQueue* pQueue) :
        commandQueue(pQueue), iqDataOutQueue(NULL), terminated(false), offset(0), deviceId(-1) {
    dev = NULL;
    sampleRate = DEFAULT_SAMPLE_RATE;
}

SDRThread::~SDRThread() {
    rtlsdr_close(dev);
}

int SDRThread::enumerate_rtl(std::vector<SDRDeviceInfo *> *devs) {

    int first_available = -1;

    char manufact[256], product[256], serial[256];

    unsigned int rtl_count = rtlsdr_get_device_count();

    std::cout << "RTL Devices: " << rtl_count << std::endl;

    for (int i = 0; i < rtl_count; i++) {
        std::string deviceName(rtlsdr_get_device_name(i));
        std::string deviceManufacturer;
        std::string deviceProduct;
        std::string deviceTuner;
        std::string deviceSerial;

        bool deviceAvailable = false;
        std::cout << "Device #" << i << ": " << deviceName << std::endl;
        if (rtlsdr_get_device_usb_strings(i, manufact, product, serial) == 0) {
            std::cout << "\tManufacturer: " << manufact << ", Product Name: " << product << ", Serial: " << serial << std::endl;

            deviceSerial = serial;
            deviceAvailable = true;
            deviceProduct = product;
            deviceManufacturer = manufact;

            rtlsdr_dev_t *devTest;
            rtlsdr_open(&devTest, i);

            std::cout << "\t Tuner type: ";
            switch (rtlsdr_get_tuner_type(devTest)) {
            case RTLSDR_TUNER_UNKNOWN:
                deviceTuner = "Unknown";
                break;
            case RTLSDR_TUNER_E4000:
                deviceTuner = "Elonics E4000";
                break;
            case RTLSDR_TUNER_FC0012:
                deviceTuner = "Fitipower FC0012";
                break;
            case RTLSDR_TUNER_FC0013:
                deviceTuner = "Fitipower FC0013";
                break;
            case RTLSDR_TUNER_FC2580:
                deviceTuner = "Fitipower FC2580";
                break;
            case RTLSDR_TUNER_R820T:
                deviceTuner = "Rafael Micro R820T";
                break;
            case RTLSDR_TUNER_R828D:
                deviceTuner = "Rafael Micro R828D";
                break;
            }

            std::cout << deviceTuner << std::endl;
            /*
             int num_gains = rtlsdr_get_tuner_gains(dev, NULL);

             int *gains = (int *)malloc(sizeof(int) * num_gains);
             rtlsdr_get_tuner_gains(dev, gains);

             std::cout << "\t Valid gains: ";
             for (int g = 0; g < num_gains; g++) {
             if (g > 0) {
             std::cout << ", ";
             }
             std::cout << ((float)gains[g]/10.0f);
             }
             std::cout << std::endl;

             free(gains);
             */

            rtlsdr_close(devTest);
            if (first_available == -1) {
                first_available = i;
            }

        } else {
            std::cout << "\tUnable to access device #" << i << " (in use?)" << std::endl;
        }

        if (devs != NULL) {
            SDRDeviceInfo *devInfo = new SDRDeviceInfo();
            devInfo->setName(deviceName);
            devInfo->setAvailable(deviceAvailable);
            devInfo->setProduct(deviceProduct);
            devInfo->setSerial(deviceSerial);
            devInfo->setManufacturer(deviceManufacturer);
            devs->push_back(devInfo);
        }
    }

    return first_available;

}

void SDRThread::threadMain() {
#ifdef __APPLE__
    pthread_t tID = pthread_self();  // ID of this thread
    int priority = sched_get_priority_max( SCHED_FIFO) - 1;
    sched_param prio = { priority }; // scheduling priority of thread
    pthread_setschedparam(tID, SCHED_FIFO, &prio);
#endif

    std::cout << "SDR thread initializing.." << std::endl;

    int devCount = rtlsdr_get_device_count();
    if (deviceId == -1) {
        deviceId = enumerate_rtl(NULL);
    }

    if (deviceId == -1) {
        std::cout << "No devices found.. SDR Thread exiting.." << std::endl;
        return;
    } else {
        std::cout << "Using device #" << deviceId << std::endl;
    }

    signed char buf[BUF_SIZE];

    long long frequency = DEFAULT_FREQ;

    rtlsdr_open(&dev, deviceId);
    rtlsdr_set_sample_rate(dev, sampleRate);
    rtlsdr_set_center_freq(dev, frequency - offset);
    rtlsdr_set_freq_correction(dev, ppm0);
    //rtlsdr_set_agc_mode(dev, 1);
    rtlsdr_set_tuner_gain_mode(dev, 1);
    rtlsdr_set_tuner_gain(dev, 340);
    rtlsdr_set_offset_tuning(dev, 0);
    rtlsdr_reset_buffer(dev);

//    sampleRate = rtlsdr_get_sample_rate(dev);

    std::cout << "Sample Rate is: " << sampleRate << std::endl;

    int n_read;
    double seconds = 0.0;

    std::cout << "SDR thread started.." << std::endl;

    std::deque<SDRThreadIQData *> buffers;
    std::deque<SDRThreadIQData *>::iterator buffers_i;

    while (!terminated) {
        SDRThreadCommandQueue *cmdQueue = commandQueue.load();

        if (!cmdQueue->empty()) {
            bool freq_changed = false;
            bool offset_changed = false;
            bool rate_changed = false;
            bool device_changed = false;
            long long new_freq;
            long long new_offset;
            long long new_rate;
            int new_device;

            while (!cmdQueue->empty()) {
                SDRThreadCommand command;
                cmdQueue->pop(command);

                switch (command.cmd) {
                case SDRThreadCommand::SDR_THREAD_CMD_TUNE:
                    freq_changed = true;
                    new_freq = command.llong_value;
                    if (new_freq < sampleRate / 2) {
                        new_freq = sampleRate / 2;
                    }
//                    std::cout << "Set frequency: " << new_freq << std::endl;
                    break;
                case SDRThreadCommand::SDR_THREAD_CMD_SET_OFFSET:
                    offset_changed = true;
                    new_offset = command.llong_value;
                    std::cout << "Set offset: " << new_offset << std::endl;
                    break;
                case SDRThreadCommand::SDR_THREAD_CMD_SET_SAMPLERATE:
                    rate_changed = true;
                    new_rate = command.llong_value;
                    std::cout << "Set sample rate: " << new_rate << std::endl;
                    break;
                case SDRThreadCommand::SDR_THREAD_CMD_SET_DEVICE:
                    device_changed = true;
                    new_device = (int) command.llong_value;
                    std::cout << "Set device: " << new_device << std::endl;
                    break;
                default:
                    break;
                }
            }

            if (device_changed) {
                rtlsdr_close(dev);
                rtlsdr_open(&dev, new_device);
                rtlsdr_set_sample_rate(dev, sampleRate);
                rtlsdr_set_center_freq(dev, frequency - offset);
                rtlsdr_set_agc_mode(dev, 1);
                rtlsdr_set_freq_correction(dev, ppm0);
                rtlsdr_set_offset_tuning(dev, 0);
                rtlsdr_reset_buffer(dev);
            }
            if (offset_changed && !freq_changed) {
                freq_changed = true;
                new_freq = frequency;
                offset = new_offset;
                rtlsdr_set_freq_correction(dev, ppm0);
            }
            if (rate_changed) {
                sampleRate = new_rate;
                rtlsdr_set_sample_rate(dev, new_rate);
                rtlsdr_set_freq_correction(dev, ppm0);
            }
            if (freq_changed) {
                frequency = new_freq;
                rtlsdr_set_center_freq(dev, frequency - offset);
            }
        }

        rtlsdr_read_sync(dev, buf, BUF_SIZE, &n_read);

        SDRThreadIQData *dataOut = NULL;

        for (buffers_i = buffers.begin(); buffers_i != buffers.end(); buffers_i++) {
            if ((*buffers_i)->getRefCount() <= 0) {
                dataOut = (*buffers_i);
                break;
            }
        }

        if (dataOut == NULL) {
            dataOut = new SDRThreadIQData;
            buffers.push_back(dataOut);
        }

//        std::lock_guard < std::mutex > lock(dataOut->m_mutex);
        dataOut->setRefCount(1);
        dataOut->frequency = frequency;
        dataOut->sampleRate = sampleRate;

        if (dataOut->data.capacity() < n_read) {
            dataOut->data.reserve(n_read);
        }

        if (dataOut->data.size() != n_read) {
            dataOut->data.resize(n_read);
        }

        memcpy(&dataOut->data[0], buf, n_read);

        double time_slice = (double) n_read / (double) sampleRate;
        seconds += time_slice;

        if (iqDataOutQueue.load() != NULL) {
            iqDataOutQueue.load()->push(dataOut);
        }
    }

    while (!buffers.empty()) {
        SDRThreadIQData *iqDataDel = buffers.front();
        buffers.pop_front();
//        std::lock_guard < std::mutex > lock(iqDataDel->m_mutex);
//        delete iqDataDel;
    }

    std::cout << "SDR thread done." << std::endl;
}

void SDRThread::terminate() {
    terminated = true;
}
