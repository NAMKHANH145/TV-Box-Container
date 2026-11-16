FROM nginx:alpine
COPY site/ /usr/share/nginx/html/
COPY site.conf /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/nginx.conf
CMD ["nginx", "-g", "daemon off;"]
