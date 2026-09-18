# NightMare MCP server. Build context is the repository root: the server reads
# docs/, src/, examples/ and library.json at runtime, and records the git
# revision it was built from.
#
#   docker build -f mcp/Dockerfile -t nightmare-mcp .

FROM node:24-alpine AS build
RUN apk add --no-cache git
WORKDIR /repo
COPY mcp/package.json mcp/package-lock.json mcp/
RUN cd mcp && npm ci --no-audit --no-fund
COPY mcp mcp
RUN cd mcp && npm run build && npm prune --omit=dev
# The revision file is what get_version reports; .git itself stays out of the
# final image.
COPY .git .git
RUN (git -C /repo rev-parse --short HEAD 2>/dev/null || echo unknown) > /repo/REVISION \
 && (git -C /repo rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown) > /repo/BRANCH

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
