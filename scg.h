
#ifndef SCG_H_
#define SCG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize 'main'. */
void scg_initialize (void);

/* Initialize a thread. */
void scg_thread_initialize (void);

/* Output the profile to stderr. */
void scg_output_profile (void);

#ifdef __cplusplus
}
#endif

#endif
