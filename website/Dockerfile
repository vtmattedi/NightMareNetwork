# NightMare Network website. Build context is the repository root, because the
# site renders ../docs at build time.
#
#   docker build -f website/Dockerfile -t nightmare-web .

FROM node:24-alpine AS build
WORKDIR /repo
COPY website/package.json website/package-lock.json website/
RUN cd website && npm ci --no-audit --no-fund
COPY library.json ./
COPY docs docs
COPY website website
RUN cd website && npm run build

FROM nginx:1.27-alpine
COPY website/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /repo/website/dist /usr/share/nginx/html
EXPOSE 80
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD wget -qO- http://127.0.0.1/health >/dev/null 2>&1 || exit 1
