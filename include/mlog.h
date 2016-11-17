/*
 * Copyright 2016 Preetam J. D'Souza
 * Copyright 2016 The Maru OS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef M_LOG_H
#define M_LOG_H

#include <stdio.h>

/* based on printk levels */
#define MLOG_CRIT       "<2>CRITICAL: "
#define MLOG_ERR        "<3>"
#define MLOG_WARNING    "<4>"
#define MLOG_INFO       "<6>"
#define MLOG_DEBUG      "<7>"

#define MLOGI(...) fprintf(stderr, MLOG_INFO __VA_ARGS__)
#define MLOGW(...) fprintf(stderr, MLOG_WARNING __VA_ARGS__)
#define MLOGE(...) fprintf(stderr, MLOG_ERR __VA_ARGS__)
#define MLOGC(...) fprintf(stderr, MLOG_CRIT __VA_ARGS__)

#ifdef DEBUG
#define MLOGD(...) fprintf(stderr, MLOG_DEBUG __VA_ARGS__)
#else
#define MLOGD(...)
#endif

#endif // M_LOG_H
