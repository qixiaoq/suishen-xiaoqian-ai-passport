FROM node:22-alpine
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev && npm cache clean --force
COPY server.js calendar.js app.js index.html ./
ENV NODE_ENV=production PORT=8787
USER node
EXPOSE 8787
CMD ["node", "server.js"]
