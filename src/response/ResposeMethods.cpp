#include "../../inc/HttpResponse.hpp"

pthread_mutex_t g_write;


int HttpResponse::GET()
{
    pthread_mutex_lock(&g_write);

    if (!file_) {
        std::cerr << "File not found" << std::endl;
        pthread_mutex_unlock(&g_write);
        return 500;
    }

    if (config_.getAutoIndex() && file_->is_directory())
    {
        headers_["Content-Type"] = "text/html; charset=UTF-8";
        body_ = file_->listDir(config_.getRequestTarget());
    }
    else
    {
        std::string mimeType = file_->getMimeType(file_->getMimeExt());
        if (mimeType.empty())
            mimeType = "application/octet-stream";
        headers_["Content-Type"] = mimeType;

        if (!charset_.empty())
            headers_["Content-Type"] += "; charset=" + charset_;

        body_ = file_->getContent();
    }
    headers_["Content-Length"] = ftos(body_.length());
    headers_["Cache-Control"] = "no-cache";

    pthread_mutex_unlock(&g_write);

    return 200;
}

int HttpResponse::POST()
{
    int status_code = 500;

    body_ = config_.getBody();

    pthread_mutex_lock(&g_write);
    if (!file_->exists()) {
        file_->createFile(body_);
        status_code = 201;
    } else {
        file_->appendFile(body_);
        status_code = 200;
    }

    pthread_mutex_unlock(&g_write);

    headers_["Content-Length"] = ftos(body_.length());

    if (!file_->getFilePath().empty())
        headers_["Location"] = file_->getFilePath();

    return status_code;
}


int HttpResponse::PUT() {
    pthread_mutex_lock(&g_write);

    int status_code = 204;

    if (!file_) {
        std::cerr << "File not found" << std::endl;
        pthread_mutex_unlock(&g_write);
        return 500;
    }

    if (file_->exists()) {
        file_->createFile(config_.getBody());
    } else {
        file_->createFile(config_.getBody());
        if (!file_->exists()) {
            std::cerr << "Failed to create file" << std::endl;
            pthread_mutex_unlock(&g_write);
            return 500;
        }
        headers_["Content-Length"] = "0";
        status_code = 201;
    }
    pthread_mutex_unlock(&g_write);
    return status_code;
}


int HttpResponse::DELETE() {
    pthread_mutex_lock(&g_write);

    int status_code = 200;

    if (!file_) {
        std::cerr << "File not found" << std::endl;
        status_code = 500;
    } else {
        if (!file_->exists()) {
            std::cerr << "File does not exist" << std::endl;
            status_code = 404;
        } else {
            if (file_->deleteFile()) {
                headers_["Content-Length"] = "0";
                status_code = 200;
            } else {
                status_code = 500;
            }
        }
    }

    pthread_mutex_unlock(&g_write);

    if (status_code == 200) {
        body_ = "<!DOCTYPE html>\n\
                 <html>\n\
                 <body>\n\
                   <h1>File deleted</h1>\n\
                 </body>\n\
                 </html>";
        headers_["Content-Type"] = "text/html";
        headers_["Content-Length"] = ftos(body_.length());
    } else {
        body_ = (status_code == 404)
            ? "<!DOCTYPE html>\n\
               <html>\n\
               <body>\n\
                 <h1>File not found</h1>\n\
               </body>\n\
               </html>"
            : "<!DOCTYPE html>\n\
               <html>\n\
               <body>\n\
                 <h1>Internal Server Error</h1>\n\
               </body>\n\
               </html>";
        headers_["Content-Type"] = "text/html";
        headers_["Content-Length"] = ftos(body_.length());
    }

    return status_code;
}
