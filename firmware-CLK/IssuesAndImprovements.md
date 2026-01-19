Main Issues and Future Improvements
==============================================:

- Fix CV calibration issues to improve accuracy of output voltages specially when using the quantizer for both quantizing generated waveforms and reading CV inputs.
- Handling the encoder, affects timing and apparently the screen updates affect the waveform generation.
- Improve code structure and modularity.
- Improve timing and scheduling of tasks to ensure smooth waveform generation.
- Implement waveform generation on the digital outputs if possible by using PWM or other techniques.
- Have some analitics and metrics to understand CPU load, memory usage, and timing issues.
- Any possible improvements and optimizations in general
- Better and more automated menu system generation from a higher level description.
- Add internal modulators like LFOs to modulate internal parameters without external CVs.
- Implement the beat loop like Pams



Future Improvements (will be worked afterwards)
==============================================:

- In the future, migrate to a dual core architecture and have a quad DAC or two dual DACs for better performance and more simultaneous outputs. This way we could have all four outputs generating waveforms.
- Maybe refactor to another language or framework that better handles timing and concurrency. Maybe Rust or Zephyr RTOS.
