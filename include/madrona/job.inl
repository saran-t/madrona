namespace madrona {

// FIXME: implement is_child and dependencies
template <typename Fn, typename... Args>
inline JobID Context::queueJob(Fn &&fn, bool is_child,
                               Args... dependencies)
{
    Job job = job_mgr_->makeJob(std::forward<Fn>(fn), worker_idx_);
    (void)is_child;

    ( (void)dependencies, ... );

    return job_mgr_->queueJob(worker_idx_, job, nullptr, 0,
                              JobPriority::Normal);
}

template <typename Fn>
JobManager::JobManager(int desired_num_workers, int num_io,
                       Fn &&fn, bool pin_workers)
    : JobManager(desired_num_workers, num_io,
        [](Context &ctx, void *data) {
            auto fn_ptr = (Fn *)data;
            (*fn_ptr)(ctx);
            fn_ptr->~Fn();
        },
        &fn,
        pin_workers)
{}

template <typename Fn>
Job JobManager::makeJob(Fn &&fn, int thread_idx)
{
    Job job;
    if constexpr (std::is_empty_v<Fn>) {
        job.func_ = [](Context &ctx, void *) {
            Fn()(ctx);
        };
        job.data_ = nullptr;
    } else {
        // Make job_size bigger to fit metadata
        static constexpr size_t job_size = sizeof(Fn);

        static constexpr size_t job_alignment = std::alignment_of_v<Fn>;
        static_assert(job_size <= Alloc::maxJobSize,
                  "Job lambda capture is too large");
        static_assert(job_alignment <= Alloc::maxJobAlignment,
                      "Job lambda capture has too large an alignment requirement");
        static_assert(utils::isPower2(job_alignment));

        void *store = job_allocs_[thread_idx].alloc(alloc_state_, job_size,
                                                    job_alignment);

        new (store) Fn(std::forward<Fn>(fn));

        job.func_ = [](Context &ctx, void *data) {
            auto fn_ptr = (Fn *)data;
            (*fn_ptr)(ctx);
            fn_ptr->~Fn();

            // Important note: jobs may be freed by different threads
            ctx.job_mgr_->deallocJob(ctx.worker_idx_, fn_ptr, job_size);
        };

        job.data_ = store;
    }

    return job;
}

JobID JobManager::queueJob(int thread_idx, Job job,
                           const JobID *deps, uint32_t num_dependencies,
                           JobPriority prio)
{
    return queueJob(thread_idx, job.func_, job.data_, deps,
                    num_dependencies, prio);
}

void JobManager::deallocJob(int worker_idx, void *ptr, uint32_t num_bytes)
{
    job_allocs_[worker_idx].dealloc(alloc_state_, ptr, num_bytes);
}

}
