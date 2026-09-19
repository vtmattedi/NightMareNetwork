---
title: Future architecture topics
description: Explicitly reserved design work beyond the current resource rewrite.
section: architecture
order: 3
---

# Future architecture topics

The current API reserves namespace in `ResourceAddress` and topic mapping without defining an isolation policy. The following topics remain open:

1. Namespace ownership, wildcard discovery and multi-cluster isolation.
2. Bridge filtering and namespace mapping between local and remote brokers.
3. Security and authorization for Value writes and Action invocations.
4. Timezone and daylight saving rules for local calendar schedules.
5. NTP and multiple wall-clock sources.
6. Resource grouping and hierarchy beyond optional metadata.
7. Advanced Action results and asynchronous result semantics.
8. Persistent Job configuration and remote Job management.
9. Multiple Runtime executors and task affinity.
10. Rich metadata balanced against embedded RAM and Flash cost.

These are deliberate follow-up design items; the active implementation does not silently choose their long-term behavior.
