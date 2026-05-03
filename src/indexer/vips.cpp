#include "../types.h"

#include <memory>
#include <format>

#include <vips/vips8>
#include <zip.h>


void vips_init(std::string_view argv0) {
    if (VIPS_INIT(argv0.data())) vips_error_exit(nullptr);
}

void read_all_metadata(ParsedManga::ImageData& data, const vips::VImage& im) {
    data.width = im.width();
    data.height = im.height();
    data.bands = im.bands();

    char** fields = vips_image_get_fields(im.get_image());
    char** fields_alloc = fields;
    while (*fields != nullptr) {
        const char* name = *fields;

        GType type = vips_image_get_typeof(im.get_image(), name);
        if (type == G_TYPE_INT) {
            data.metadata.emplace(std::string(name), im.get_int(name));
        } else if (type == G_TYPE_DOUBLE) {
            data.metadata.emplace(std::string(name), im.get_double(name));
        } else if (type == VIPS_TYPE_REF_STRING) {
            data.metadata.emplace(std::string(name), im.get_string(name));
        } else if (type == VIPS_TYPE_ARRAY_INT) {
            data.metadata.emplace(std::string(name), im.get_array_int(name));
        } else if (type == VIPS_TYPE_ARRAY_DOUBLE) {
            data.metadata.emplace(std::string(name), im.get_array_double(name));
        /*
        } else if (type == VIPS_TYPE_BLOB) {
            std::size_t blob_size = 0;
            const void* blob = im.get_blob(name, &blob_size);
            std::vector<unsigned char> bytes(
                (const unsigned char*) blob,
                (const unsigned char*) blob + blob_size
            );
            data.metadata.emplace(std::string(name), MetadataBlob{std::move(bytes)});
        */
        } else {
            char* stringified;
            vips_image_get_as_string(im.get_image(), name, &stringified);
            data.metadata.emplace(std::string(name), 
                std::format("<{}: {}>", g_type_name(type), stringified)
            );
            g_free(stringified);
        }
        ++fields;
    }
    g_strfreev(fields_alloc);
}


std::vector<ParsedManga::ImageData> get_image_data(ParsedManga& m) {
    std::unique_ptr<unsigned char[]> buf;
    std::size_t bufsize = 0;

    auto data = std::vector(m.images.size(), ParsedManga::ImageData {
        .width = (std::size_t) -1,
        .height = (std::size_t) -1,
        .bands = (std::size_t) -1
    });

    for (auto& [_, archive] : m.archives) {
        if (archive.images.empty()) continue;

        int zip_error;
        zip_t* zip = zip_openwitherror(archive.path.data(), 0, 'r', &zip_error);

        if (zip_error) {
            m.errs.add(BadArchiveError{ .path = archive.path, .message = zip_strerror(zip_error) }, archive.errors);
            continue;
        }

        for (const std::size_t i : archive.images) {
            auto& image = m.images[i];
            auto& datum = data[i];

            const int err = zip_entry_open(zip, image.filename.data());
            if (err) {
                m.errs.add(BadArchiveImageReadError { 
                    .path = archive.path, .filename = image.filename,
                    .message = zip_strerror(err)
                }, image.errors, archive.errors);
                continue;
            }

            const std::size_t size = zip_entry_size(zip);
            if (bufsize < size) {
                bufsize = 3 * size / 2;
                buf = std::make_unique<unsigned char[]>(bufsize);
            }
            
            zip_entry_noallocread(zip, buf.get(), bufsize);
            zip_entry_close(zip);

            try {
                vips::VImage im = vips::VImage::new_from_buffer(buf.get(), size, "",
                    vips::VImage::option()->set("access", VIPS_ACCESS_SEQUENTIAL));

                read_all_metadata(datum, im);
            } catch (const vips::VError& e) {
                m.errs.add(ImageLoadFailedError {
                    .path = archive.path, .filename = image.filename,
                    .message = e.what()
                }, image.errors, archive.errors);
            }
        }

        zip_close(zip);
    }
    return data;
}

