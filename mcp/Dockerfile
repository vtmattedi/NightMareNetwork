# NightMare MCP server. Build context is the repository root: the server reads
# docs/, src/, examples/ and library.json at runtime, and records the git
# revision it was built from when the context carries a .git.
#
#   docker build -f mcp/Dockerfile -t nightmare-mcp .

FROM node:24-alpine AS build
RUN apk add --no-cache git
WORKDIR /repo
COPY mcp/package.json mcp/package-lock.json mcp/
RUN cd mcp && npm ci --no-audit --no-fund
COPY mcp mcp
RUN cd mcp && npm run build && npm prune --omit=dev
# The whole context, so the revision is read when a .git is present and its
# absence (an exported or shallow checkout) is not a build failure. The clone
# is owned by another uid than the build runs as, which git refuses by default.
COPY . /ctx
RUN git config --global --add safe.directory '*' \
 && (git -C /ctx rev-parse --short HEAD 2>/dev/null || echo unknown) > /repo/REVISION \
 && (git -C /ctx rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown) > /repo/BRANCH \
 && echo "revision $(cat /repo/REVISION) on $(cat /repo/BRANCH)"

FROM node:24-alpine
ENV NODE_ENV=production \
    PORT=3000 \
    NM_REPO_ROOT=/app
WORKDIR /app
COPY --from=build /repo/mcp/dist mcp/dist
COPY --from=build /repo/mcp/node_modules mcp/node_modules
COPY --from=build /repo/mcp/package.json mcp/package.json
COPY --from=build /repo/REVISION /repo/BRANCH ./
COPY library.json ./
COPY docs docs
COPY src src
COPY examples examples
EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:3000/health').then(r=>{if(!r.ok)process.exit(1)}).catch(()=>process.exit(1))"
CMD ["node", "mcp/dist/http.js"]
