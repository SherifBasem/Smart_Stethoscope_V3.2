#ifndef STETHO_CONFIG_H
#define STETHO_CONFIG_H

#ifndef STETHO_DEBUG_LOGS
#define STETHO_DEBUG_LOGS 0
#endif

#ifndef STETHO_HF_PREDICT_PCM_URL
#define STETHO_HF_PREDICT_PCM_URL "https://sherox2345-respiratory-disease-classification.hf.space/predict-pcm"
#endif

#ifndef STETHO_HF_TOKEN
#define STETHO_HF_TOKEN ""
#endif

#ifndef STETHO_HF_AUDIO_TYPE_LUNG
#define STETHO_HF_AUDIO_TYPE_LUNG "lung"
#endif

#ifndef STETHO_HF_UPLOAD_TIMEOUT_MS
#define STETHO_HF_UPLOAD_TIMEOUT_MS 120000UL
#endif

#ifndef STETHO_HF_UPLOAD_RETRIES
#define STETHO_HF_UPLOAD_RETRIES 2
#endif

#endif /* STETHO_CONFIG_H */
