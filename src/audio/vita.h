

void vitaaudio_start();
void vitaaudio_stop();

// Audio packets that reached the decoder since launch, for the benchmark
// report. Written on common-c's audio thread: read it with vitaaudio_packet_count().
unsigned int vitaaudio_packet_count(void);
