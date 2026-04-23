/**
 * Data Layer implementation.
 *
 * Uses nlohmann::json for persistence and std::mutex for thread safety.
 * All functions are free functions operating on the Collection struct.
 */
#include "data.h"

#include <algorithm>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

namespace mcm::data {

namespace {

/**
 * clampTitle - truncates an oversize title to MAX_TITLE_LENGTH. Internal.
 */
std::string clampTitle(const std::string& title) {
    if (title.size() <= MAX_TITLE_LENGTH) {
        return title;
    }
    return title.substr(0, MAX_TITLE_LENGTH);
}

/**
 * toJson - converts a Movie into a JSON object. Internal.
 */
nlohmann::json toJson(const Movie& movie) {
    return nlohmann::json{
        {"id", movie.id},
        {"title", movie.title},
        {"year", movie.year},
        {"rating", movie.rating},
        {"durationMinutes", movie.durationMinutes},
    };
}

/**
 * fromJson - parses a JSON object into a Movie. Internal.
 * Missing fields default to zero.
 */
Movie fromJson(const nlohmann::json& node) {
    Movie movie;
    movie.id = node.value("id", std::uint64_t{0});
    movie.title = node.value("title", std::string{});
    movie.year = node.value("year", 0);
    movie.rating = node.value("rating", 0.0f);
    movie.durationMinutes = node.value("durationMinutes", 0);
    return movie;
}

} // namespace

std::uint64_t addMovie(Collection& collection, const Movie& prototype) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    Movie stored = prototype;
    stored.id = collection.nextId++;
    stored.title = clampTitle(stored.title);
    collection.movies.push_back(std::move(stored));
    ++collection.revision;
    return collection.movies.back().id;
}

bool addMovieWithId(Collection& collection, const Movie& movie) {
    if (movie.id == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(collection.mutex);
    for (const Movie& existing : collection.movies) {
        if (existing.id == movie.id) {
            return false;
        }
    }
    Movie stored = movie;
    stored.title = clampTitle(stored.title);
    collection.movies.push_back(std::move(stored));
    if (collection.nextId <= movie.id) {
        collection.nextId = movie.id + 1;
    }
    ++collection.revision;
    return true;
}

bool updateMovie(Collection& collection, const Movie& movie) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    for (Movie& existing : collection.movies) {
        if (existing.id == movie.id) {
            existing.title = clampTitle(movie.title);
            existing.year = movie.year;
            existing.rating = movie.rating;
            existing.durationMinutes = movie.durationMinutes;
            ++collection.revision;
            return true;
        }
    }
    return false;
}

bool removeMovie(Collection& collection, std::uint64_t id) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    auto iterator = std::find_if(
        collection.movies.begin(),
        collection.movies.end(),
        [id](const Movie& movie) { return movie.id == id; });
    if (iterator == collection.movies.end()) {
        return false;
    }
    collection.movies.erase(iterator);
    ++collection.revision;
    return true;
}

std::vector<Movie> snapshotMovies(const Collection& collection) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    return collection.movies;
}

std::uint64_t currentRevision(const Collection& collection) {
    std::lock_guard<std::mutex> lock(collection.mutex);
    return collection.revision;
}

std::size_t findIndexById(const std::vector<Movie>& movies, std::uint64_t id) {
    for (std::size_t index = 0; index < movies.size(); ++index) {
        if (movies[index].id == id) {
            return index;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

bool loadFromFile(Collection& collection, const std::string& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        return false;
    }
    nlohmann::json root;
    try {
        stream >> root;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }

    std::lock_guard<std::mutex> lock(collection.mutex);
    collection.movies.clear();
    collection.nextId = 1;
    if (root.contains("movies") && root["movies"].is_array()) {
        for (const auto& node : root["movies"]) {
            Movie movie = fromJson(node);
            if (movie.id == 0) {
                movie.id = collection.nextId;
            }
            if (collection.nextId <= movie.id) {
                collection.nextId = movie.id + 1;
            }
            collection.movies.push_back(std::move(movie));
        }
    }
    ++collection.revision;
    return true;
}

bool saveToFile(const Collection& collection, const std::string& path) {
    std::vector<Movie> snapshot;
    std::uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(collection.mutex);
        snapshot = collection.movies;
        revision = collection.revision;
    }

    nlohmann::json root;
    root["revision"] = revision;
    root["movies"] = nlohmann::json::array();
    for (const Movie& movie : snapshot) {
        root["movies"].push_back(toJson(movie));
    }

    std::ofstream stream(path, std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }
    stream << root.dump(2);
    return stream.good();
}

} // namespace mcm::data
