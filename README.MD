# RAVen - **R**otating **A**symetric **V**ector **En**coding

This is really just a proof of concept for another audio compression idea that I came up with.

While this tries to shoot for the same goal as [the previous codec idea](https://github.com/Wuerfel21/habane), that being reasonable quality stereo music at ~240kbps (= one 512 byte block per 60Hz video frame) and low decoder complexity, it goes about it a different way.

Rather than encoding a 24kHz stereo ADPCM stream and papering over the loss of HF content, this codec operates at 32kHz, which should be enough to cover most people's hearing range. However, to keep the bitrate down, this means that only one channel can be encoded at 32kHz, the other must be 16kHz. The main channel is chosen per 32 sample unit and can not just be left or right but also front (L+R) and back (L-R). The front/L+R channel being the main channel is the most generally useful mode. This is the basic operating principle. In actuality, the sub channel can be encoded at 32kHz, too, at the expense of being reduced to 2 bits per sample. There is also a "mono mode" that is used when the sub channel is silent, enhancing the main channel to 6 bits per sample.

Each 512 byte data block encodes 610 samples @ 32kHz (19 units of 32 compressed samples + 2 raw 16 bit samples to allow independent decoding of blocks), resulting in a rate of ~52 blocks per second or 215 kbps. Pretty good, right?
