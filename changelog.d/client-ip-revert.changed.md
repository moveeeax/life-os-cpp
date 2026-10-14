The image's own nginx config no longer reads X-Forwarded-For (added in 1.25.0
on a wrong diagnosis); the cluster uses the chart's config and never changed.
